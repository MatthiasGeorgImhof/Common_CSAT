#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "BoxSet.hpp"
#include "cyphal.hpp"
#include "cyphal_mtu.hpp"
#include "canard_adapter.hpp"
#include "Socket.hpp"
#include <iostream>
#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>
#include <malloc.h>

#ifdef __x86_64__
#include "mock_hal.h" // Include the mock HAL header
#endif

void* canardMemoryAllocate(void* /*ins*/, size_t amount)
{ 
    auto pointer = static_cast<void *>(malloc(amount));
    // printf("alloc: %p\n", pointer);
    return pointer; 
};

void  canardMemoryFree(void* /*ins*/, size_t /*amount*/, void *pointer) 
{ 
    // printf("free:  %p\n", pointer);
    free(pointer); 
};

struct CanardMemoryResource canard_memory = { nullptr, canardMemoryFree, canardMemoryAllocate};

size_t get_heap_usage(void) {
    struct mallinfo2 mi = mallinfo2();
    return mi.uordblks;
}

TEST_CASE("Canard Send Receive - small with SMALL_DATA_MTU")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = SMALL_DATA_MTU;
    
    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, PORT_ID, 100, 2000000) == 1);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello";
    size_t payload_size = sizeof(payload);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 1);

    CanardTxQueueItem *peek_ptr = canardTxPeek(&adapter.que);
    CHECK(peek_ptr != nullptr);
    CanardTxQueueItem *ptr = canardTxPop(&adapter.que, peek_ptr);
    CHECK(ptr != nullptr);
    printf("frame size: %3lu payload size: %3lu mtu %3d\n", peek_ptr->frame.payload.size, sizeof(payload), MTU);

    CyphalTransfer transfer;
    CHECK(cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer) == 1);
    CHECK(strncmp(payload, static_cast<const char *>(transfer.payload), sizeof(payload)) == 0);
    canardTxFree(&adapter.que, &adapter.ins, peek_ptr);
    canardMemoryFree(nullptr, transfer.payload_size, transfer.payload);

    cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, PORT_ID);
}

TEST_CASE("Canard Send Receive - max with SMALL_DATA_MTU")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = SMALL_DATA_MTU;
    
    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, PORT_ID, 100, 2000000) == 1);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello!";
    size_t payload_size = sizeof(payload);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 1);

    CanardTxQueueItem *peek_ptr = canardTxPeek(&adapter.que);
    CHECK(peek_ptr != nullptr);
    CanardTxQueueItem *ptr = canardTxPop(&adapter.que, peek_ptr);
    CHECK(ptr != nullptr);
    printf("frame size: %3lu payload size: %3lu mtu %3d\n", peek_ptr->frame.payload.size, sizeof(payload), MTU);

    CyphalTransfer transfer;
    CHECK(cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer) == 1);
    CHECK(strncmp(payload, static_cast<const char *>(transfer.payload), sizeof(payload)) == 0);
    canardTxFree(&adapter.que, &adapter.ins, peek_ptr);
    canardMemoryFree(nullptr, transfer.payload_size, transfer.payload);

    cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, PORT_ID);
}

TEST_CASE("Canard Send Receive - large with SMALL_DATA_MTU")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = SMALL_DATA_MTU;
    
    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, PORT_ID, 100, 2000000) == 1);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello_hello_hello_hello_hello_elloh";
    size_t payload_size = sizeof(payload);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) >= 1);

    CanardTxQueueItem *peek_ptr = nullptr;
    int32_t result{};
    CyphalTransfer transfer{};
    while ((peek_ptr = canardTxPeek(&adapter.que)))
    {
        CHECK(peek_ptr != nullptr);
        CanardTxQueueItem *ptr = canardTxPop(&adapter.que, peek_ptr);
        CHECK(ptr != nullptr);
        printf("frame size: %3lu payload size: %3lu mtu %3d\n", peek_ptr->frame.payload.size, sizeof(payload), MTU);

        result = cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer);
        canardTxFree(&adapter.que, &adapter.ins, peek_ptr);
    }
    CHECK(result == 1);
    CHECK(strncmp(payload, static_cast<const char *>(transfer.payload), payload_size) == 0);
    canardMemoryFree(nullptr, transfer.payload_size, transfer.payload);
    cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, PORT_ID);

}

TEST_CASE("Canard Send Receive - large with custom MTU 13")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = 17;
    
    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, PORT_ID, 100, 2000000) == 1);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello_hello_hello_hello_hello_elloh";
    size_t payload_size = sizeof(payload);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) >= 1);

    CanardTxQueueItem *peek_ptr = nullptr;
    int32_t result{};
    CyphalTransfer transfer{};
    while ((peek_ptr = canardTxPeek(&adapter.que)))
    {
        CHECK(peek_ptr != nullptr);
        CanardTxQueueItem *ptr = canardTxPop(&adapter.que, peek_ptr);
        CHECK(ptr != nullptr);
        printf("frame size: %3lu payload size: %3lu mtu %3d\n", peek_ptr->frame.payload.size, sizeof(payload), MTU);

        result = cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer);
        canardTxFree(&adapter.que, &adapter.ins, peek_ptr);
    }
    CHECK(result == 1);
    CHECK(strncmp(payload, static_cast<const char *>(transfer.payload), payload_size) == 0);
    canardMemoryFree(nullptr, transfer.payload_size, transfer.payload);
    cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, PORT_ID);

}

TEST_CASE("Canard Send Receive - small with LARGE_DATA_MTU")
{
    constexpr CyphalPortID PORT_ID = 123;
    constexpr uint8_t MTU = 19;

    CanardAdapter adapter;
    adapter.ins = canardInit(canard_memory);
    adapter.ins.node_id = 11;
    adapter.que = canardTxInit(16, MTU, canard_memory);
    Cyphal<CanardAdapter> cyphal(&adapter);

    CHECK(cyphal.cyphalRxSubscribe(CyphalTransferKindMessage, PORT_ID, 100, 2000000) == 1);

    CyphalTransferMetadata metadata;
    metadata.priority = CyphalPriorityNominal;
    metadata.transfer_kind = CyphalTransferKindMessage;
    metadata.port_id = PORT_ID;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.remote_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.source_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.destination_node_id = CYPHAL_NODE_ID_UNSET;
    metadata.transfer_id = 0;

    const char payload[] = "hello";
    size_t payload_size = sizeof(payload);
    CHECK(cyphal.cyphalTxPush(0, &metadata, payload_size, payload) == 1);

    CanardTxQueueItem *peek_ptr = canardTxPeek(&adapter.que);
    CHECK(peek_ptr != nullptr);
    CanardTxQueueItem *ptr = canardTxPop(&adapter.que, peek_ptr);
    CHECK(ptr != nullptr);
    printf("frame size: %3lu payload size: %3lu mtu %3d\n", peek_ptr->frame.payload.size, sizeof(payload), MTU);

    CyphalTransfer transfer;
    CHECK(cyphal.cyphalRxReceive(ptr->frame.extended_can_id, &ptr->frame.payload.size, static_cast<const uint8_t *>(ptr->frame.payload.data), &transfer) == 1);
    CHECK(strncmp(payload, static_cast<const char *>(transfer.payload), sizeof(payload)) == 0);
    canardTxFree(&adapter.que, &adapter.ins, peek_ptr);
    canardMemoryFree(nullptr, transfer.payload_size, transfer.payload);

    cyphal.cyphalRxUnsubscribe(CyphalTransferKindMessage, PORT_ID);
}
