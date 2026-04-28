#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "mock_hal.h" // For x86 testing

#include <memory>
#include "stdio.h"
#include "stdint.h"
#include "string.h"

#include "o1heap.h"
#include "serard.h"

#include "cyphal.hpp"
#include "serard_adapter.hpp"
#include "loopard_adapter.hpp"

#ifndef NUNAVUT_ASSERT
#define NUNAVUT_ASSERT(x) assert(x)
#endif

#include "cyphal_subscriptions.hpp"
#include <CircularBuffer.hpp>
#include <ArrayList.hpp>
#include "HeapAllocation.hpp"
#include "RegistrationManager.hpp"
#include "SubscriptionManager.hpp"
#include "ServiceManager.hpp"
#include "ProcessRxQueue.hpp"
#include "TaskCheckMemory.hpp"
#include "TaskBlinkLED.hpp"
#include "TaskSendHeartBeat.hpp"
#include "TaskProcessHeartBeat.hpp"
#include "TaskSendNodePortList.hpp"
#include "TaskSubscribeNodePortList.hpp"
#include "TaskRespondGetInfo.hpp"
#include "TaskRequestGetInfo.hpp"

#include "TrivialImageBuffer.hpp"
#include "TaskSyntheticImageGenerator.hpp"
#include "Trigger.hpp"
#include "InputOutputStream.hpp"
#include "TaskRequestWrite.hpp"

#include "CC1101.hpp"
#include "CC1101Manager.hpp"

#include "Logger.hpp"
#include "MessageAccumulator.hpp"
#include "FrameAssembler.hpp"

#include "nunavut_assert.h"
#include "uavcan/node/Heartbeat_1_0.h"

SerardAdapter serard_adapter;
SerardAdapter radio_adapter;
LoopardAdapter loopard_adapter;

#ifndef CYPHAL_NODE_ID
#define CYPHAL_NODE_ID 41
#endif

constexpr CyphalNodeID cyphal_node_id = CYPHAL_NODE_ID;

// using LocalHeap = HeapAllocation<65536>;

constexpr uint32_t SERIAL_TIMEOUT = 1000;
constexpr size_t SERIAL_BUFFER_SIZE = 16;
using SerialCircularBuffer = CircularBuffer<SerialFrame, SERIAL_BUFFER_SIZE>;
SerialCircularBuffer serial_buffer;

template <typename T, typename... Args>
static void register_task_with_heap(RegistrationManager &rm, Args &&...args)
{
    static SafeAllocator<T, LocalHeap> alloc;
    rm.add(alloc_unique_custom<T, LocalHeap>(alloc, std::forward<Args>(args)...));
}

#ifdef __cplusplus
extern "C"
{
#endif
    bool dummy_emit(void * /*user_ref*/, uint8_t /*data_size*/, const uint8_t * /*data*/)
    {
        return true;
    }
#ifdef __cplusplus
}
#endif

TEST_CASE("Downstream Leak")
{
    LocalHeap::initialize();

    using SerardCyphal = Cyphal<SerardAdapter>;
    struct SerardMemoryResource serard_memory_resource = {&serard_adapter.ins, LocalHeap::serardMemoryAllocate, LocalHeap::serardMemoryDeallocate};
    serard_adapter.ins = serardInit(serard_memory_resource, serard_memory_resource);
    serard_adapter.reass = serardReassemblerInit();
    serard_adapter.emitter = dummy_emit;
    SerardCyphal serard_cyphal(&serard_adapter);
    serard_cyphal.setNodeID(cyphal_node_id);

    std::tuple<Cyphal<SerardAdapter>> serard_adapters = {serard_cyphal};
    std::tuple<> empty_adapters = {};

    RegistrationManager registration_manager;
    SubscriptionManager subscription_manager;

    LoopManager<LocalHeap> loop_manager;
    O1HeapInstance *o1heap = LocalHeap::getO1Heap();

    using TPHeart = TaskProcessHeartBeat<SerardCyphal>;
    register_task_with_heap<TPHeart>(registration_manager, 500U, 100U, serard_adapters);

    using TCheckMem = TaskCheckMemory;
    register_task_with_heap<TCheckMem>(registration_manager, o1heap, 500U, 100U);

    subscription_manager.subscribeAll(registration_manager, serard_adapters);

    ServiceManager service_manager(registration_manager.getHandlers());
    service_manager.initializeServices(0);
    HAL_SetTick(1000);

    // SUBCASE("Empty Serial Buffer")
    // {
    //     O1HeapDiagnostics diagnostic_before = o1heapGetDiagnostics(o1heap);
    //     loop_manager.ProcessRxQueue(&serard_cyphal, &service_manager, empty_adapters, serial_buffer);
    //     service_manager.handleServices();
    //     O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);

    //     CHECK(diagnostic_before.allocated == diagnostic_after.allocated);
    // }

    // SUBCASE("ProcessTransfer with Empty Payload")
    // {
    //     O1HeapDiagnostics diagnostic_before = o1heapGetDiagnostics(o1heap);

    //     CyphalTransfer transfer;
    //     transfer.metadata.port_id = 1234;
    //     transfer.payload_size = 0;
    //     transfer.payload = nullptr;
    //     loop_manager.processTransfer(transfer, &service_manager, empty_adapters);
    //     service_manager.handleServices();
    //     O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);

    //     CHECK(diagnostic_before.allocated == diagnostic_after.allocated);
    // }

    // SUBCASE("ProcessTransfer with Non-Empty Payload")
    // {
    //     O1HeapDiagnostics diagnostic_before = o1heapGetDiagnostics(o1heap);

    //     constexpr size_t payload_size = 10;
    //     uint8_t *payload = static_cast<uint8_t *>(LocalHeap::heapAllocate(nullptr, payload_size));
    //     O1HeapDiagnostics diagnostic_payload = o1heapGetDiagnostics(o1heap);
    //     CHECK(diagnostic_before.allocated != diagnostic_payload.allocated);

    //     for (size_t i = 0; i < payload_size; ++i)
    //     {
    //         payload[i] = static_cast<uint8_t>(i);
    //     }
    //     CyphalTransfer transfer;
    //     transfer.metadata.port_id = 1234;
    //     transfer.payload_size = payload_size;
    //     transfer.payload = payload;

    //     loop_manager.processTransfer(transfer, &service_manager, empty_adapters);
    //     O1HeapDiagnostics diagnostic_shared = o1heapGetDiagnostics(o1heap);
    //     CHECK(diagnostic_payload.allocated != diagnostic_shared.allocated);

    //     service_manager.handleServices();
    //     O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);
    //     CHECK(diagnostic_before.allocated == diagnostic_after.allocated);
    // }

    // SUBCASE("ProcessTransfer with Heartbeat Payload")
    // {
    //     O1HeapDiagnostics diagnostic_before = o1heapGetDiagnostics(o1heap);
    //     {
    //         uavcan_node_Heartbeat_1_0 data = {
    //             .uptime = 1234,
    //             .health = {uavcan_node_Health_1_0_NOMINAL},
    //             .mode = {uavcan_node_Mode_1_0_OPERATIONAL},
    //             .vendor_specific_status_code = 0};
    //         // @formatter:on

    //         size_t payload_size = uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
    //         uint8_t *payload = static_cast<uint8_t *>(LocalHeap::heapAllocate(nullptr, payload_size));
    //         O1HeapDiagnostics diagnostic_payload = o1heapGetDiagnostics(o1heap);
    //         CHECK(diagnostic_before.allocated != diagnostic_payload.allocated);

    //         CHECK(uavcan_node_Heartbeat_1_0_serialize_(&data, payload, &payload_size) == NUNAVUT_SUCCESS);

    //         CyphalTransfer transfer;
    //         transfer.metadata.port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_;
    //         transfer.payload_size = payload_size;
    //         transfer.payload = payload;

    //         loop_manager.processTransfer(transfer, &service_manager, empty_adapters);
    //         O1HeapDiagnostics diagnostic_shared = o1heapGetDiagnostics(o1heap);
    //         CHECK(diagnostic_payload.allocated != diagnostic_shared.allocated);

    //         service_manager.handleServices();
    //     }
    //     O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);
    //     CHECK(diagnostic_before.allocated == diagnostic_after.allocated);
    // }

    // SUBCASE("ProcessTransfer with Heartbeat Payload - Stability Test")
    // {
    //     constexpr size_t payload_size = uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;

    //     // 1. Warm up the system (let subscribers allocate their "last value" buffers)
    //     {
    //         uint8_t *payload = static_cast<uint8_t *>(LocalHeap::heapAllocate(nullptr, payload_size));
    //         CyphalTransfer transfer;
    //         transfer.metadata.port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_;
    //         transfer.payload_size = payload_size;
    //         transfer.payload = payload;
    //         loop_manager.processTransfer(transfer, &service_manager, empty_adapters);
    //         service_manager.handleServices();
    //     }

    //     // 2. Capture baseline after the system has "learned" the heartbeat
    //     O1HeapDiagnostics diagnostic_baseline = o1heapGetDiagnostics(o1heap);

    //     HAL_SetTick(2000);
    //     // 3. Send a second heartbeat
    //     {
    //         uint8_t *payload = static_cast<uint8_t *>(LocalHeap::heapAllocate(nullptr, payload_size));
    //         CyphalTransfer transfer;
    //         transfer.metadata.port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_;
    //         transfer.payload_size = payload_size;
    //         transfer.payload = payload;
    //         loop_manager.processTransfer(transfer, &service_manager, empty_adapters);
    //         service_manager.handleServices();
    //     }

    //     // 4. Check if memory returned to the baseline
    //     O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);

    //     // This should pass because the shared_ptr in the Task was swapped/updated
    //     CHECK(diagnostic_baseline.allocated == diagnostic_after.allocated);
    // }

    // SUBCASE("ProcessRxQueue with Heartbeat Payloads")
    // {
    //     CircularBuffer<SerialFrame, 4> buffer;

    //     static uint8_t hb_frame[] =
    //         {
    //             0x00, 0x04, 0x01, 0x04, 0x79, 0x06, 0xff, 0xff, 0x55, 0x1d, 0x17, 0x01,
    //             0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x80, 0x01, 0x04,
    //             0x0e, 0x10, 0x17, 0x01, 0x01, 0x01, 0x01, 0x01, 0x05, 0x42, 0x20, 0x80,
    //             0x3a, 0x00};

    //     O1HeapDiagnostics diagnostic_before = o1heapGetDiagnostics(o1heap);
    //     {
    //         SerialFrame frame;
    //         frame.size = sizeof(hb_frame);
    //         memcpy(frame.data, hb_frame, sizeof(hb_frame));
    //         buffer.push(frame);

    //         loop_manager.ProcessRxQueue(&serard_cyphal, &service_manager, empty_adapters, buffer);

    //         O1HeapDiagnostics diagnostic_mid = o1heapGetDiagnostics(o1heap);
    //         CHECK(diagnostic_mid.allocated > diagnostic_before.allocated);

    //         service_manager.handleServices();
    //     }
    //     O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);
    //     CHECK(diagnostic_after.allocated != diagnostic_before.allocated);

    //     for (uint8_t i = 1; i < 10; ++i)
    //     {
    //         HAL_SetTick(i * 1000);
    //         SerialFrame frame;
    //         frame.size = sizeof(hb_frame);
    //         memcpy(frame.data, hb_frame, sizeof(hb_frame));
    //         buffer.push(frame);

    //         loop_manager.ProcessRxQueue(&serard_cyphal, &service_manager, empty_adapters, buffer);

    //         O1HeapDiagnostics diagnostic_mid = o1heapGetDiagnostics(o1heap);
    //         CHECK(diagnostic_mid.allocated > diagnostic_before.allocated);

    //         service_manager.handleServices();
    //         O1HeapDiagnostics diagnostic = o1heapGetDiagnostics(o1heap);
    //         CHECK(diagnostic.allocated != diagnostic_after.allocated);
    //     }
    // }

    SUBCASE("ProcessRxQueue with Heartbeat Payloads - Stability Test 1")
    {
        for (uint32_t i = 0; i < 10; i++)
        {
            {
                std::cerr << "START Iteration " << i << ": allocated = " << o1heapGetDiagnostics(o1heap).allocated << std::endl;

                HAL_SetTick(i * 1000);
                static uint8_t hb_frame[] =
                    {
                        0x00, 0x04, 0x01, 0x04, 0x79, 0x06, 0xff, 0xff, 0x55, 0x1d, 0x17, 0x01,
                        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x80, 0x01, 0x04,
                        0x0e, 0x10, 0x17, 0x01, 0x01, 0x01, 0x01, 0x01, 0x05, 0x42, 0x20, 0x80,
                        0x3a, 0x00};

                SerialFrame frame;
                frame.size = sizeof(hb_frame);
                memcpy(frame.data, hb_frame, sizeof(hb_frame));
                serial_buffer.push(frame);

                loop_manager.ProcessRxQueue(&serard_cyphal, &service_manager, empty_adapters, serial_buffer);
                service_manager.handleServices();

                std::cerr << "END Iteration " << i << ": allocated = " << o1heapGetDiagnostics(o1heap).allocated << std::endl;
            }
        }
    }

}
