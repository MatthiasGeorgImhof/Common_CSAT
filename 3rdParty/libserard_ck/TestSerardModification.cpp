#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

extern "C" {
#include "serard.h"
}

extern "C" {

// ---------------- Memory resources ----------------

static void* test_alloc_payload(void* user, size_t size)
{
    (void)user;
    return malloc(size);
}

static void test_free_payload(void* user, size_t size, void* ptr)
{
    (void)user;
    (void)size;
    free(ptr);
}

static void* test_alloc_session(void* user, size_t size)
{
    (void)user;
    return malloc(size);
}

static void test_free_session(void* user, size_t size, void* ptr)
{
    (void)user;
    (void)size;
    free(ptr);
}

} // extern "C"

// ---------------- Helpers ----------------

static SerardRx make_instance(uint16_t node_id,
                              uint16_t fwd_start,
                              uint16_t fwd_end)
{
    SerardMemoryResource payload_mem{};
    payload_mem.user_reference = nullptr;
    payload_mem.allocate       = test_alloc_payload;
    payload_mem.deallocate     = test_free_payload;

    SerardMemoryResource session_mem{};
    session_mem.user_reference = nullptr;
    session_mem.allocate       = test_alloc_session;
    session_mem.deallocate     = test_free_session;

    SerardRx ins = serardInit(payload_mem, session_mem);
    ins.node_id          = node_id;
    ins.forward_start_id = fwd_start;
    ins.forward_end_id   = fwd_end;
    return ins;
}

// TX emitter: collect fragments into a buffer
struct TxBuffer
{
    std::vector<uint8_t> data;
};

static bool tx_emitter(void* user, uint8_t size, const uint8_t* bytes)
{
    TxBuffer* buf = reinterpret_cast<TxBuffer*>(user);
    buf->data.insert(buf->data.end(), bytes, bytes + size);
    return true;
}

static void pump_rx_stream(SerardRx& rx_ins,
                           const std::vector<uint8_t>& stream,
                           SerardRxTransfer* out,
                           SerardRxSubscription** out_sub,
                           int8_t* result)
{
    SerardReassembler r = serardReassemblerInit();

    size_t remaining = stream.size();
    const uint8_t* ptr = stream.data();

    while (remaining > 0)
    {
        size_t chunk = remaining;
        int8_t rcv = serardRxAccept(&rx_ins,
                                    &r,
                                    /*timestamp*/ 123456,
                                    &chunk,
                                    ptr,
                                    /*redundant_transport_index*/ 0,
                                    out,
                                    out_sub);

        if (rcv != 0)
        {
            *result = rcv;
            return;
        }

        ptr += chunk;
        remaining -= chunk;
    }

    *result = 0;
}

// ---------------- Tests ----------------

// 1. Anonymous message
TEST_CASE("Anonymous message is received")
{
    SerardRx tx = make_instance(SERARD_NODE_ID_UNSET, 0, 0);
    SerardRx rx = make_instance(11, 0, 0);

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindMessage,
                              500,
                              1024,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t payload[3] = {1,2,3};

    SerardTransferMetadata meta{};
    meta.priority       = SerardPriorityNominal;
    meta.transfer_kind  = SerardTransferKindMessage;
    meta.port_id        = 500;
    meta.remote_node_id = SERARD_NODE_ID_UNSET;
    meta.transfer_id    = 0;

    TxBuffer buf;
    REQUIRE(serardTxPush(tx.node_id, &meta, sizeof(payload), payload, &buf, tx_emitter) > 0);

    SerardRxTransfer out{};
    SerardRxSubscription* out_sub = nullptr;
    int8_t result = 0;

    pump_rx_stream(rx, buf.data, &out, &out_sub, &result);

    CHECK(result == 1);
    CHECK(out_sub == &sub);
    CHECK(out.metadata.remote_node_id == SERARD_NODE_ID_UNSET);
}

// 2. Message from node 42
TEST_CASE("Message from node 42 is received")
{
    SerardRx tx = make_instance(42, 0, 0);
    SerardRx rx = make_instance(11, 0, 0);

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindMessage,
                              500,
                              1024,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t payload[3] = {1,2,3};

    SerardTransferMetadata meta{};
    meta.priority       = SerardPriorityNominal;
    meta.transfer_kind  = SerardTransferKindMessage;
    meta.port_id        = 500;
    meta.remote_node_id = SERARD_NODE_ID_UNSET;
    meta.transfer_id    = 0;

    TxBuffer buf;
    REQUIRE(serardTxPush(tx.node_id, &meta, sizeof(payload), payload, &buf, tx_emitter) > 0);

    SerardRxTransfer out{};
    SerardRxSubscription* out_sub = nullptr;
    int8_t result = 0;

    pump_rx_stream(rx, buf.data, &out, &out_sub, &result);

    CHECK(result == 1);
    CHECK(out_sub == &sub);
    CHECK(out.metadata.source_node_id == 42);
}

// 3. Request 42 → 11
TEST_CASE("Request 42 -> 11 is received")
{
    SerardRx tx = make_instance(42, 0, 0);
    SerardRx rx = make_instance(11, 0, 0);

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindRequest,
                              400,
                              1024,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t payload[3] = {1,2,3};

    SerardTransferMetadata meta{};
    meta.priority       = SerardPriorityNominal;
    meta.transfer_kind  = SerardTransferKindRequest;
    meta.port_id        = 400;
    meta.remote_node_id = 11;
    meta.transfer_id    = 0;

    TxBuffer buf;
    REQUIRE(serardTxPush(tx.node_id, &meta, sizeof(payload), payload, &buf, tx_emitter) > 0);

    SerardRxTransfer out{};
    SerardRxSubscription* out_sub = nullptr;
    int8_t result = 0;

    pump_rx_stream(rx, buf.data, &out, &out_sub, &result);

    CHECK(result == 1);
    CHECK(out_sub == &sub);
    CHECK(out.metadata.source_node_id == 42);
}

// 4. Response 42 → 11
TEST_CASE("Response 42 -> 11 is received")
{
    SerardRx tx = make_instance(42, 0, 0);
    SerardRx rx = make_instance(11, 0, 0);

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindResponse,
                              400,
                              1024,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t payload[3] = {1,2,3};

    SerardTransferMetadata meta{};
    meta.priority       = SerardPriorityNominal;
    meta.transfer_kind  = SerardTransferKindResponse;
    meta.port_id        = 400;
    meta.remote_node_id = 11;
    meta.transfer_id    = 0;

    TxBuffer buf;
    REQUIRE(serardTxPush(tx.node_id, &meta, sizeof(payload), payload, &buf, tx_emitter) > 0);

    SerardRxTransfer out{};
    SerardRxSubscription* out_sub = nullptr;
    int8_t result = 0;

    pump_rx_stream(rx, buf.data, &out, &out_sub, &result);

    CHECK(result == 1);
    CHECK(out_sub == &sub);
}

// 5. Request 42 → 121 (forwarding window)
TEST_CASE("Request 42 -> 121 is received via forwarding window")
{
    SerardRx tx = make_instance(42, 0, 0);
    SerardRx rx = make_instance(11, 120, 130);

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindRequest,
                              400,
                              1024,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t payload[3] = {1,2,3};

    SerardTransferMetadata meta{};
    meta.priority       = SerardPriorityNominal;
    meta.transfer_kind  = SerardTransferKindRequest;
    meta.port_id        = 400;
    meta.remote_node_id = 121;
    meta.transfer_id    = 0;

    TxBuffer buf;
    REQUIRE(serardTxPush(tx.node_id, &meta, sizeof(payload), payload, &buf, tx_emitter) > 0);

    SerardRxTransfer out{};
    SerardRxSubscription* out_sub = nullptr;
    int8_t result = 0;

    pump_rx_stream(rx, buf.data, &out, &out_sub, &result);

    CHECK(result == 1);
    CHECK(out_sub == &sub);
}

// 6. Response 42 → 121 (forwarding window)
TEST_CASE("Response 42 -> 121 is received via forwarding window")
{
    SerardRx tx = make_instance(42, 0, 0);
    SerardRx rx = make_instance(11, 120, 130);

    SerardRxSubscription sub{};
    REQUIRE(serardRxSubscribe(&rx,
                              SerardTransferKindResponse,
                              400,
                              1024,
                              SERARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                              &sub) >= 0);

    uint8_t payload[3] = {1,2,3};

    SerardTransferMetadata meta{};
    meta.priority       = SerardPriorityNominal;
    meta.transfer_kind  = SerardTransferKindResponse;
    meta.port_id        = 400;
    meta.remote_node_id = 121;
    meta.transfer_id    = 0;

    TxBuffer buf;
    REQUIRE(serardTxPush(tx.node_id, &meta, sizeof(payload), payload, &buf, tx_emitter) > 0);

    SerardRxTransfer out{};
    SerardRxSubscription* out_sub = nullptr;
    int8_t result = 0;

    pump_rx_stream(rx, buf.data, &out, &out_sub, &result);

    CHECK(result == 1);
    CHECK(out_sub == &sub);
}
