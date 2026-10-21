#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

extern "C" {
#include "canard.h"
}

extern "C" {

static void* test_allocate(CanardInstance* ins, size_t amount)
{
    (void)ins;
    return malloc(amount);
}

static void test_free(CanardInstance* ins, void* pointer)
{
    (void)ins;
    free(pointer);
}

} // extern "C"

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static CanardInstance make_instance(uint8_t node_id,
                                    uint8_t fwd_start,
                                    uint8_t fwd_end)
{
    CanardInstance ins = canardInit(&test_allocate, &test_free);
    ins.node_id          = node_id;
    ins.forward_start_id = fwd_start;
    ins.forward_end_id   = fwd_end;
    return ins;
}

static CanardTxQueue make_tx_queue(size_t capacity, size_t mtu)
{
    return canardTxInit(capacity, mtu);
}

static int32_t push_service_request(CanardTxQueue&        txq,
                                    CanardInstance&       tx_ins,
                                    uint8_t               src_node_id,
                                    uint8_t               dst_node_id,
                                    CanardPortID          service_id,
                                    CanardTransferID      tid,
                                    const uint8_t*        payload,
                                    size_t                payload_size)
{
    tx_ins.node_id = src_node_id;

    CanardTransferMetadata meta{};
    meta.priority       = CanardPriorityNominal;
    meta.transfer_kind  = CanardTransferKindRequest;
    meta.port_id        = service_id;
    meta.remote_node_id = dst_node_id;

    const CanardMicrosecond deadline = 1'000'000; // arbitrary

    return canardTxPush(&txq,
                        &tx_ins,
                        deadline,
                        &meta,
                        payload_size,
                        payload);
}

static int8_t pump_one_frame(CanardTxQueue&        txq,
                             CanardInstance&       tx_ins,
                             CanardInstance&       rx_ins,
                             uint8_t               redundant_iface_index,
                             CanardRxTransfer*     out_transfer,
                             CanardRxSubscription** out_sub)
{
    const CanardTxQueueItem* item = canardTxPeek(&txq);
    if (!item)
    {
        return 0; // nothing to send
    }

    // TX frame -> RX frame (they share the same struct type)
    const CanardFrame* frame = &item->frame;

    int8_t r = canardRxAccept(&rx_ins,
                              item->tx_deadline_usec, // use deadline as timestamp
                              frame,
                              redundant_iface_index,
                              out_transfer,
                              out_sub);

    // Pop and free TX item
    (void)canardTxPop(&txq, item);
    tx_ins.memory_free(&tx_ins, (void*)item);

    return r;
}

// ------------------------------------------------------------
// Tests
// ------------------------------------------------------------

TEST_CASE("Service request delivered when destination matches local node-ID")
{
    // TX node: 42, RX node: 11
    auto tx_ins = make_instance(/*node*/ 42, /*fwd_start*/ 0,   /*fwd_end*/ 0);
    auto rx_ins = make_instance(/*node*/ 11, /*fwd_start*/ 0,   /*fwd_end*/ 0);

    CanardTxQueue txq = make_tx_queue(/*capacity*/ 8, CANARD_MTU_CAN_FD);

    // RX subscribes to service-ID 400 (requests)
    CanardRxSubscription sub_400{};
    int8_t sr = canardRxSubscribe(&rx_ins,
                                  CanardTransferKindRequest,
                                  /*port_id*/ 400,
                                  /*extent*/ 1024,
                                  CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                                  &sub_400);
    REQUIRE(sr >= 0);

    uint8_t payload[3] = {1, 2, 3};

    // Push a request from 42 -> 11 on service 400
    int32_t pushed = push_service_request(txq,
                                          tx_ins,
                                          /*src*/ 42,
                                          /*dst*/ 11,
                                          /*service*/ 400,
                                          /*tid*/ 0,
                                          payload,
                                          sizeof(payload));
    REQUIRE(pushed > 0);

    CanardRxTransfer      out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 1);
    CHECK(out_sub == &sub_400);
    CHECK(out.metadata.transfer_kind == CanardTransferKindRequest);
    CHECK(out.metadata.port_id == 400);
    CHECK(out.metadata.remote_node_id == 42);
}

TEST_CASE("Service request accepted when destination is inside forwarding window")
{
    // RX node is 11, but forwarding window covers 120..130
    auto tx_ins = make_instance(/*node*/ 42, /*fwd_start*/ 0,   /*fwd_end*/ 0);
    auto rx_ins = make_instance(/*node*/ 11, /*fwd_start*/ 120, /*fwd_end*/ 130);

    CanardTxQueue txq = make_tx_queue(/*capacity*/ 8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_400{};
    int8_t sr = canardRxSubscribe(&rx_ins,
                                  CanardTransferKindRequest,
                                  /*port_id*/ 400,
                                  /*extent*/ 1024,
                                  CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                                  &sub_400);
    REQUIRE(sr >= 0);

    uint8_t payload[3] = {1, 2, 3};

    // Push a request from 42 -> 121 (NOT 11) on service 400
    int32_t pushed = push_service_request(txq,
                                          tx_ins,
                                          /*src*/ 42,
                                          /*dst*/ 121,
                                          /*service*/ 400,
                                          /*tid*/ 0,
                                          payload,
                                          sizeof(payload));
    REQUIRE(pushed > 0);

    CanardRxTransfer      out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    // This will be 1 only if your modified canardRxAccept()
    // allows frames whose destination is in [forward_start_id, forward_end_id)
    CHECK(r == 1);
    CHECK(out_sub == &sub_400);
    CHECK(out.metadata.remote_node_id == 42);
}

TEST_CASE("Service request accepted when destination is inside forwarding window: multiframe payload")
{
    // RX node is 11, but forwarding window covers 120..130
    auto tx_ins = make_instance(/*node*/ 42, /*fwd_start*/ 0,   /*fwd_end*/ 0);
    auto rx_ins = make_instance(/*node*/ 11, /*fwd_start*/ 120, /*fwd_end*/ 130);

    CanardTxQueue txq = make_tx_queue(/*capacity*/ 8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_400{};
    int8_t sr = canardRxSubscribe(&rx_ins,
                                  CanardTransferKindRequest,
                                  /*port_id*/ 400,
                                  /*extent*/ 1024,
                                  CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                                  &sub_400);
    REQUIRE(sr >= 0);

    uint8_t payload[256];
    for(size_t i=0; i<sizeof(payload); ++i) payload[i] = i;

    // Push a request from 42 -> 121 (NOT 11) on service 400
    int32_t pushed = push_service_request(txq,
                                          tx_ins,
                                          /*src*/ 42,
                                          /*dst*/ 121,
                                          /*service*/ 400,
                                          /*tid*/ 0,
                                          payload,
                                          sizeof(payload));
    REQUIRE(pushed > 0);

    CanardRxTransfer      out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r{};

    do
    {
        r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);
    } 
    while (r == 0);
    
    CHECK(r == 1);
    CHECK(out_sub == &sub_400);
    CHECK(out.metadata.remote_node_id == 42);
}

TEST_CASE("Service request rejected when destination is outside forwarding window")
{
    auto tx_ins = make_instance(/*node*/ 42, /*fwd_start*/ 0,   /*fwd_end*/ 0);
    auto rx_ins = make_instance(/*node*/ 11, /*fwd_start*/ 120, /*fwd_end*/ 130);

    CanardTxQueue txq = make_tx_queue(/*capacity*/ 8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_400{};
    int8_t sr = canardRxSubscribe(&rx_ins,
                                  CanardTransferKindRequest,
                                  /*port_id*/ 400,
                                  /*extent*/ 1024,
                                  CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                                  &sub_400);
    REQUIRE(sr >= 0);

    uint8_t payload[3] = {1, 2, 3};

    // Push a request from 42 -> 50 (outside forwarding window, not local)
    int32_t pushed = push_service_request(txq,
                                          tx_ins,
                                          /*src*/ 42,
                                          /*dst*/ 50,
                                          /*service*/ 400,
                                          /*tid*/ 0,
                                          payload,
                                          sizeof(payload));
    REQUIRE(pushed > 0);

    CanardRxTransfer      out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 0);      // mis-addressed: dropped by your modified canardRxAccept()
    CHECK(out_sub == nullptr);
}

TEST_CASE("Anonymous message is received")
{
    auto tx_ins = make_instance(/*node*/ 42, 0, 0);
    tx_ins.node_id = CANARD_NODE_ID_UNSET;

    auto rx_ins = make_instance(/*node*/ 11, 0, 0);

    CanardTxQueue txq = make_tx_queue(8, CANARD_MTU_CAN_FD);


    // Subscribe to subject-ID 500
    CanardRxSubscription sub_msg{};
    REQUIRE(canardRxSubscribe(&rx_ins,
                              CanardTransferKindMessage,
                              500,
                              1024,
                              CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub_msg) >= 0);

    // Anonymous message: remote_node_id = CANARD_NODE_ID_UNSET
    CanardTransferMetadata meta{};
    meta.priority       = CanardPriorityNominal;
    meta.transfer_kind  = CanardTransferKindMessage;
    meta.port_id        = 500;
    meta.remote_node_id = CANARD_NODE_ID_UNSET;

    uint8_t payload[3] = {1,2,3};

    REQUIRE(canardTxPush(&txq, &tx_ins, 1000000, &meta, sizeof(payload), payload) > 0);

    CanardRxTransfer out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 1);
    CHECK(out_sub == &sub_msg);
    CHECK(out.metadata.remote_node_id == CANARD_NODE_ID_UNSET);
}

TEST_CASE("Message from node 42 is received")
{
    auto tx_ins = make_instance(42, 0, 0);
    auto rx_ins = make_instance(11, 0, 0);

    CanardTxQueue txq = make_tx_queue(8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_msg{};
    REQUIRE(canardRxSubscribe(&rx_ins,
                              CanardTransferKindMessage,
                              500,
                              1024,
                              CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub_msg) >= 0);

    CanardTransferMetadata meta{};
    meta.priority       = CanardPriorityNominal;
    meta.transfer_kind  = CanardTransferKindMessage;
    meta.port_id        = 500;
    meta.remote_node_id = CANARD_NODE_ID_UNSET;

    uint8_t payload[3] = {1,2,3};

    REQUIRE(canardTxPush(&txq, &tx_ins, 1000000, &meta, sizeof(payload), payload) > 0);

    CanardRxTransfer out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 1);
    CHECK(out_sub == &sub_msg);
    CHECK(out.metadata.remote_node_id == 42);
}

TEST_CASE("Request 42 -> 11 is received")
{
    auto tx_ins = make_instance(42, 0, 0);
    auto rx_ins = make_instance(11, 0, 0);

    CanardTxQueue txq = make_tx_queue(8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_req{};
    REQUIRE(canardRxSubscribe(&rx_ins,
                              CanardTransferKindRequest,
                              400,
                              1024,
                              CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub_req) >= 0);

    uint8_t payload[3] = {1,2,3};

    REQUIRE(push_service_request(txq, tx_ins, 42, 11, 400, 0, payload, sizeof(payload)) > 0);

    CanardRxTransfer out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 1);
    CHECK(out_sub == &sub_req);
    CHECK(out.metadata.remote_node_id == 42);
}

TEST_CASE("Response 42 -> 11 is received")
{
    auto tx_ins = make_instance(42, 0, 0);
    auto rx_ins = make_instance(11, 0, 0);

    CanardTxQueue txq = make_tx_queue(8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_resp{};
    REQUIRE(canardRxSubscribe(&rx_ins,
                              CanardTransferKindResponse,
                              400,
                              1024,
                              CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub_resp) >= 0);

    uint8_t payload[3] = {1,2,3};

    // Build response metadata
    CanardTransferMetadata meta{};
    meta.priority       = CanardPriorityNominal;
    meta.transfer_kind  = CanardTransferKindResponse;
    meta.port_id        = 400;
    meta.remote_node_id = 11;   // client node-ID
    meta.source_node_id = 42;   // server node-ID

    REQUIRE(canardTxPush(&txq, &tx_ins, 1000000, &meta, sizeof(payload), payload) > 0);

    CanardRxTransfer out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 1);
    CHECK(out_sub == &sub_resp);
}

TEST_CASE("Request 42 -> 121 is received by 11 via forwarding window")
{
    auto tx_ins = make_instance(42, 0, 0);
    auto rx_ins = make_instance(11, 120, 130); // forwarding window

    CanardTxQueue txq = make_tx_queue(8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_req{};
    REQUIRE(canardRxSubscribe(&rx_ins,
                              CanardTransferKindRequest,
                              400,
                              1024,
                              CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub_req) >= 0);

    uint8_t payload[3] = {1,2,3};

    REQUIRE(push_service_request(txq, tx_ins, 42, 121, 400, 0, payload, sizeof(payload)) > 0);

    CanardRxTransfer out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 1);
    CHECK(out_sub == &sub_req);
}

TEST_CASE("Response 42 -> 121 is received by 11 via forwarding window")
{
    auto tx_ins = make_instance(42, 0, 0);
    auto rx_ins = make_instance(11, 120, 130);

    CanardTxQueue txq = make_tx_queue(8, CANARD_MTU_CAN_FD);

    CanardRxSubscription sub_resp{};
    REQUIRE(canardRxSubscribe(&rx_ins,
                              CanardTransferKindResponse,
                              400,
                              1024,
                              CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub_resp) >= 0);

    uint8_t payload[3] = {1,2,3};

    CanardTransferMetadata meta{};
    meta.priority       = CanardPriorityNominal;
    meta.transfer_kind  = CanardTransferKindResponse;
    meta.port_id        = 400;
    meta.remote_node_id = 121;  // client
    meta.source_node_id = 42;   // server

    REQUIRE(canardTxPush(&txq, &tx_ins, 1000000, &meta, sizeof(payload), payload) > 0);

    CanardRxTransfer out{};
    CanardRxSubscription* out_sub = nullptr;

    int8_t r = pump_one_frame(txq, tx_ins, rx_ins, 0, &out, &out_sub);

    CHECK(r == 1);
    CHECK(out_sub == &sub_resp);
}
