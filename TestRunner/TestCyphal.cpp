#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "BoxSet.hpp"
#include "cyphal.hpp"
#include "canard_adapter.hpp"
#include "loopard_adapter.hpp"
#include <iostream>
#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>

#ifdef __x86_64__
#include "mock_hal.h" // Include the mock HAL header
#endif

void* canardMemoryAllocate(void* /*ins*/, size_t amount) { return static_cast<void *>(malloc(amount)); };
void  canardMemoryFree(void* /*ins*/, size_t /*amount*/, void *pointer) { free(pointer); };

struct CanardMemoryResource canard_memory = { nullptr, canardMemoryFree, canardMemoryAllocate};

TEST_CASE("Canard Adapter")
{
    CanardAdapter adapter;

    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello";
    size_t payload_size = sizeof(payload);

    SUBCASE("cyphalTxPush")
    {
        CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 1);
    }

    SUBCASE("getNodeID and setNodeID")
    {
        CHECK(cyphal.getNodeID() == 11);
        cyphal.setNodeID(22);
        CHECK(cyphal.getNodeID() == 22);
    }

    SUBCASE("cyphalTxForward")
    {
        CanardAdapter adapter2;
        adapter2.ins = canardInit(canard_memory);
        adapter2.ins.node_id = 11;
        adapter2.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);

        Cyphal<CanardAdapter> cyphal2(&adapter2);
        const char payload[] = "hello";
        size_t payload_size = sizeof(payload);

        auto res = cyphal2.cyphalTxForward(0, &metadata, payload_size, payload, 33);
        CHECK(res == 1);
    }

    REQUIRE(adapter.subscriptions.size() == 0);
    SUBCASE("cyphalRxSubscribe and Unsubscribe")
    {

        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 42, 100, 2000000) == 1);
        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 42, 100, 2000000) == 0);
        CHECK(cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, 42) == 1);
        CHECK(cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, 42) == 0);
        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 43, 100, 2000000) == 1);
        CHECK(cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, 43) == 1);
        CHECK(cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, 43) == 0);
    }
    REQUIRE(adapter.subscriptions.size() == 0);

    SUBCASE("cyphalRxSubscribe returns negative on full boxset")
    {
        adapter.subscriptions.clear();
        for (CyphalPortID i = 0; i < CanardAdapter::SUBSCRIPTIONS; ++i)
        {
            CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, i, 100, 2000000) == 1);
        }
        REQUIRE(adapter.subscriptions.is_full());
        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, CanardAdapter::SUBSCRIPTIONS + 1, 100, 2000000) == -2);
    }
}

TEST_CASE("Canard Send Receive")
{
    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload1[] = "hello";
    size_t payload_size1 = sizeof(payload1);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size1, payload1) == 1);

    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id++;

    const char payload2[] = "ehllo ehllo ehllo";
    size_t payload_size2 = sizeof(payload2);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size2, payload2) == 3);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 123, 100, 2000000) == 1);

    CanardTxQueueItem *peek_ptr = canardTxPeek(&adapter.que);
    CHECK(peek_ptr != nullptr);
    CanardTxQueueItem *ptr = canardTxPop(&adapter.que, peek_ptr);
    CHECK(ptr != nullptr);

    CyphalTransfer transfer;

    CHECK(cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer) == 1);
    CHECK(strncmp(payload1, static_cast<const char *>(transfer.payload), 5) == 0);

    CanardTxQueueItem *peek_ptr2 = canardTxPeek(&adapter.que);
    CHECK(peek_ptr2 != nullptr);
    ptr = canardTxPop(&adapter.que, peek_ptr2);
    CHECK(ptr != nullptr);
    CHECK(cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer) == 0);

    CanardTxQueueItem *const peek_ptr3 = canardTxPeek(&adapter.que);
    CHECK(peek_ptr3 != nullptr);
    ptr = canardTxPop(&adapter.que, peek_ptr3);
    CHECK(ptr != nullptr);
    CHECK(cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer) == 0);

    CanardTxQueueItem *peek_ptr4 = canardTxPeek(&adapter.que);
    CHECK(peek_ptr4 != nullptr);
    ptr = canardTxPop(&adapter.que, peek_ptr4);
    CHECK(ptr != nullptr);
    CHECK(cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer) == 1);
    CHECK(strncmp(payload2, static_cast<const char *>(transfer.payload), 18) == 0);
}

TEST_CASE("Canard Send Receive Large")
{
    CAN_HandleTypeDef hcan;
    constexpr CyphalPortID port_id = 123;
    constexpr CyphalNodeID node_id = 11;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = node_id;
    adapter.que = canardTxInit(64, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, port_id, 512, 2000000) == 1);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = port_id;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    char payload[256];
    for (size_t i = 0; i < sizeof(payload); ++i)
    {
        payload[i] = static_cast<char>(i);
    }
    size_t payload_size = sizeof(payload);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 37);

    for (CanardTxQueueItem *ti = NULL; (ti = canardTxPeek(&adapter.que)) != NULL;)
    {
        CAN_TxHeaderTypeDef header;
        header.ExtId = ti->frame.extended_can_id;
        header.DLC = static_cast<uint8_t>(ti->frame.payload.size);
        header.RTR = CAN_RTR_DATA;
        header.IDE = CAN_ID_EXT;
        uint32_t mailbox;
        CHECK(HAL_CAN_AddTxMessage(&hcan, &header, static_cast<uint8_t *>(const_cast<void *>(ti->frame.payload.data)), &mailbox) == HAL_OK);
        canardTxPop(&adapter.que, ti);
        canardTxFree(&adapter.que, &adapter.ins, ti);    }

    move_can_tx_to_rx();

    CyphalTransfer transfer;
    for (int i = 0; i < 37; ++i)
    {
        CAN_RxHeaderTypeDef header;
        uint8_t data[8];
        CHECK(HAL_CAN_GetRxMessage(&hcan, 0, &header, data) == HAL_OK);
        size_t data_size = header.DLC;
        int32_t result = cyphal.cyphalRxReceive(header.ExtId, &data_size, data, &transfer);
        if (i < 36)
            CHECK(result == 0);
        else
            CHECK(result == 1);
    }
    CHECK(transfer.payload_size == 256);
    CHECK(transfer.metadata.port_id == port_id);
    CHECK(transfer.metadata.remote_node_id == node_id);
    CHECK(transfer.metadata.transfer_kind == CyphalTransferKindMessage);
    CHECK(transfer.metadata.transfer_id == 0);
    CHECK(transfer.metadata.priority == CyphalPriorityNominal);
    CHECK(transfer.payload != nullptr);
    CHECK(memcmp(transfer.payload, payload, sizeof(payload)) == 0);
    adapter.ins.memory.deallocate(&adapter.ins, transfer.payload_size, transfer.payload);
}

TEST_CASE("Canard Send Forward Receive")
{
    constexpr CyphalNodeID my_id = 11;
    constexpr CyphalNodeID forward_id = 22;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = my_id;
    adapter.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);
    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 123, 100, 2000000) == 1);

    CyphalTransfer transfer1, transfer2;

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = forward_id;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload1[] = "hello";
    size_t payload_size1 = sizeof(payload1);
    auto res = cyphal.cyphalTxForward(0, &metadata, payload_size1, payload1, 0);
    CHECK(res == 1);
    CanardTxQueueItem *peek_ptr1 = canardTxPeek(&adapter.que);
    CanardTxQueueItem *ptr1 = canardTxPop(&adapter.que, peek_ptr1);
    CHECK(cyphal.cyphalRxReceive(ptr1->frame.extended_can_id, &ptr1->frame.payload.size, static_cast<const uint8_t *>(ptr1->frame.payload.data), &transfer1) == 1);
    CHECK(strncmp(payload1, static_cast<const char *>(transfer1.payload), 5) == 0);
    CHECK(transfer1.metadata.remote_node_id == forward_id);

    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = forward_id;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id++;

    const char payload2[] = "ehllo";
    size_t payload_size2 = sizeof(payload2);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size2, payload2) == 1);
    CanardTxQueueItem *peek_ptr2 = canardTxPeek(&adapter.que);
    CanardTxQueueItem *ptr2 = canardTxPop(&adapter.que, peek_ptr2);
    CHECK(cyphal.cyphalRxReceive(ptr2->frame.extended_can_id, &ptr2->frame.payload.size, static_cast<const uint8_t *>(ptr2->frame.payload.data), &transfer2) == 1);
    CHECK(strncmp(payload2, static_cast<const char *>(transfer2.payload), 5) == 0);
    CHECK(transfer2.metadata.remote_node_id == my_id);
}

void *loopardMemoryAllocate(size_t amount) { return static_cast<void *>(malloc(amount)); };
void loopardMemoryFree(void *pointer) { free(pointer); };

TEST_CASE("Loopard Adapter")
{
    LoopardAdapter adapter;
    adapter.memory_allocate = loopardMemoryAllocate;
    adapter.memory_free = loopardMemoryFree;
    Cyphal<LoopardAdapter> cyphal(&adapter);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello";
    size_t payload_size = sizeof(payload);

    SUBCASE("cyphalTxPush: Successful push")
    {
        CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 1);
        REQUIRE(adapter.buffer.size() == 1);
        const auto &transfer = adapter.buffer.peek();
        CHECK(transfer.metadata.port_id == metadata.port_id);
        CHECK(transfer.metadata.priority == metadata.priority);
        CHECK(transfer.payload_size == payload_size);
        CHECK(std::memcmp(transfer.payload, payload, payload_size) == 0);
    }
    SUBCASE("cyphalTxPush: returns 0 when buffer full")
    {
        // fill the buffer
        for (size_t i = 0; i < LoopardAdapter::BUFFER; ++i)
        {
            CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 1);
        }
        CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 0);
    }

    SUBCASE("cyphalRxSubscribe and Unsubscribe")
    {

        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 42, 100, 2000000) == 1);
        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 42, 100, 2000000) == 1);
        CHECK(cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, 42) == 1);
        CHECK(cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, 42) == 1);
        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 43, 100, 2000000) == 1);
        CHECK(cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, 43) == 1);
    }

    REQUIRE(adapter.subscriptions.size() == 0);
    SUBCASE("cyphalRxSubscribe returns negative on full boxset")
    {
        adapter.subscriptions.clear();
        for (CyphalPortID i = 0; i < LoopardAdapter::SUBSCRIPTIONS; ++i)
        {
            CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, i, 100, 2000000) == 1);
        }
        REQUIRE(adapter.subscriptions.is_full());
        CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, LoopardAdapter::SUBSCRIPTIONS + 1, 100, 2000000) == 1);
    }
}

TEST_CASE("Loopard Send Receive")
{
    LoopardAdapter adapter;
    adapter.memory_allocate = loopardMemoryAllocate;
    adapter.memory_free = loopardMemoryFree;
    Cyphal<LoopardAdapter> cyphal(&adapter);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload1[] = "hello";
    size_t payload_size1 = sizeof(payload1);

    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size1, payload1) == 1);
    REQUIRE(adapter.buffer.size() == 1);

    const char payload2[] = "ehllo ";
    size_t payload_size2 = sizeof(payload2);
    metadata.transfer_id++;
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size2, payload2) == 1);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 123, 100, 2000000) == 1);

    CyphalTransfer transfer = {};
    size_t inout_payload_size = 0;
    CHECK(cyphal.cyphalRxReceive(nullptr, &inout_payload_size, &transfer) == 2);
    CHECK(transfer.payload_size == payload_size1);
    CHECK(strncmp(payload1, static_cast<const char *>(transfer.payload), 5) == 0);
    CHECK(cyphal.cyphalRxReceive(nullptr, &inout_payload_size, &transfer) == 1);
    CHECK(transfer.payload_size == payload_size2);
    CHECK(strncmp(payload2, static_cast<const char *>(transfer.payload), 5) == 0);
    CHECK(cyphal.cyphalRxReceive(nullptr, &inout_payload_size, &transfer) == 0);
}

TEST_CASE("Loopard Forward Send Receive")
{
    CyphalNodeID my_id = 11;
    CyphalNodeID forward_id = 22;

    LoopardAdapter adapter;
    adapter.memory_allocate = loopardMemoryAllocate;
    adapter.memory_free = loopardMemoryFree;
    Cyphal<LoopardAdapter> cyphal(&adapter);
    adapter.node_id = my_id;

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = 123;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload1[] = "hello";
    size_t payload_size1 = sizeof(payload1);

    CHECK(cyphal.cyphalTxForward(0, &metadata, payload_size1, payload1, forward_id) == 1);
    REQUIRE(adapter.buffer.size() == 1);

    const char payload2[] = "ehllo ";
    size_t payload_size2 = sizeof(payload2);
    metadata.transfer_id++;
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size2, payload2) == 1);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, 123, 100, 2000000) == 1);

    CyphalTransfer transfer = {};
    size_t inout_payload_size = 0;
    CHECK(cyphal.cyphalRxReceive(nullptr, &inout_payload_size, &transfer) == 2);
    CHECK(transfer.payload_size == payload_size1);
    CHECK(strncmp(payload1, static_cast<const char *>(transfer.payload), 5) == 0);
    CHECK(transfer.metadata.remote_node_id == forward_id);

    CHECK(cyphal.cyphalRxReceive(nullptr, &inout_payload_size, &transfer) == 1);
    CHECK(transfer.payload_size == payload_size2);
    CHECK(strncmp(payload2, static_cast<const char *>(transfer.payload), 5) == 0);
    CHECK(cyphal.cyphalRxReceive(nullptr, &inout_payload_size, &transfer) == 0);
    CHECK(transfer.metadata.remote_node_id == my_id);
}

template <class A1, class A2>
class TestClass
{
private:
    std::tuple<A1, A2> adapters_;

public:
    TestClass(std::tuple<A1, A2> adapters) : adapters_(adapters) {}

    int8_t txpush_unroll(const size_t frame_size, void const *frame)
    {
        CyphalTransferMetadata metadata;
        metadata.priority = CyphalPriorityNominal;
        metadata.transfer_kind = CyphalTransferKindMessage;
        metadata.port_id = 123;
        metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.transfer_id = 0;

        int32_t res0 = std::get<0>(adapters_).cyphalTxPush(0, &metadata, frame_size, frame);
        int32_t res1 = std::get<1>(adapters_).cyphalTxPush(0, &metadata, frame_size, frame);
        return (res0 > 0) && (res1 > 0);
    }

    int8_t txpush_loop(const size_t frame_size, void const *frame)
    {
        CyphalTransferMetadata metadata;
        metadata.priority = CyphalPriorityNominal;
        metadata.transfer_kind = CyphalTransferKindMessage;
        metadata.port_id = 123;
        metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
        metadata.transfer_id = 0;

        bool all_successful = true;
        std::apply([&](auto &...adapter)
                 { ([&]()
                    {
                        int32_t res = adapter.cyphalTxPush(0, &metadata, frame_size, frame);
                        all_successful = all_successful && (res > 0); 
                    }(), ...);
                 }, adapters_);
        return all_successful;
    }
};

TEST_CASE("All Combined Unroll")
{
    LoopardAdapter loopard_adapter;
    loopard_adapter.memory_allocate = loopardMemoryAllocate;
    loopard_adapter.memory_free = loopardMemoryFree;
    Cyphal<LoopardAdapter> loopard_cyphal(&loopard_adapter);

    CanardAdapter canard_adapter;
    canard_adapter.ins = canardInit(canard_memory);
    canard_adapter.ins.node_id = 11;
    canard_adapter.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> canard_cyphal(&canard_adapter);

    auto adapters = TestClass(std::tuple<Cyphal<LoopardAdapter>, Cyphal<CanardAdapter>>{loopard_cyphal, canard_cyphal});

    const char frame[] = "common message";
    size_t frame_size = sizeof(frame);
    CHECK(adapters.txpush_unroll(frame_size, frame) == 1);

    CHECK(loopard_adapter.buffer.size() == 1);
    CHECK(canard_adapter.que.size > 0);
}

TEST_CASE("All Combined Loop")
{
    LoopardAdapter loopard_adapter;
    loopard_adapter.memory_allocate = loopardMemoryAllocate;
    loopard_adapter.memory_free = loopardMemoryFree;
    Cyphal<LoopardAdapter> loopard_cyphal(&loopard_adapter);

    CanardAdapter canard_adapter;
    canard_adapter.ins = canardInit(canard_memory);
    canard_adapter.ins.node_id = 11;
    canard_adapter.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> canard_cyphal(&canard_adapter);

    auto adapters = TestClass(std::tuple<Cyphal<LoopardAdapter>, Cyphal<CanardAdapter>>{loopard_cyphal, canard_cyphal});

    const char frame[] = "common message";
    size_t frame_size = sizeof(frame);
    CHECK(adapters.txpush_loop(frame_size, frame) == 1);

    CHECK(loopard_adapter.buffer.size() == 1);
    CHECK(canard_adapter.que.size > 0);
}

//
//
//

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::stringstream ss(hex);
    std::string byteString;

    while (ss >> byteString) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(byteString, nullptr, 16)));
    }
    return bytes;
}
