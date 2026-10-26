#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "mock_hal.h" // For x86 testing

#include <memory>
#include "stdio.h"
#include "stdint.h"
#include "string.h"

#include "o1heap.h"
#include "canard.h"

#include "cyphal.hpp"
#include "canard_adapter.hpp"
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

CanardAdapter canard_adapter;
CanardAdapter radio_adapter;
LoopardAdapter loopard_adapter;

#ifndef CYPHAL_NODE_ID
#define CYPHAL_NODE_ID 41
#endif

constexpr CyphalNodeID cyphal_node_id = CYPHAL_NODE_ID;

template <typename T, typename... Args>
static void register_task_with_heap(RegistrationManager &rm, Args &&...args)
{
    static SafeAllocator<T, LocalHeap> alloc;
    rm.add(alloc_unique_custom<T, LocalHeap>(alloc, std::forward<Args>(args)...));
}

TEST_CASE("Downstream Leak")
{
    LocalHeap::initialize();
    struct CanardMemoryResource canard_memory = { nullptr, LocalHeap::canardMemoryDeallocate, LocalHeap::canardMemoryAllocate};

    canard_adapter.ins = canardInit(canard_memory);
    canard_adapter.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> canard_cyphal(&canard_adapter);
    canard_cyphal.setNodeID(cyphal_node_id);


    using CanardCyphal = Cyphal<CanardAdapter>;

    std::tuple<Cyphal<CanardAdapter>> canard_adapters = {canard_cyphal};
    std::tuple<> empty_adapters = {};

    RegistrationManager registration_manager;
    SubscriptionManager subscription_manager;

    LoopManager<LocalHeap> loop_manager;
    O1HeapInstance *o1heap = LocalHeap::getO1Heap();

    using TPHeart = TaskProcessHeartBeat<CanardCyphal>;
    register_task_with_heap<TPHeart>(registration_manager, 500U, 100U, canard_adapters);

    using TCheckMem = TaskCheckMemory;
    register_task_with_heap<TCheckMem>(registration_manager, o1heap, 500U, 100U);

    subscription_manager.subscribeAll(registration_manager, canard_adapters);

    ServiceManager service_manager(registration_manager.getHandlers());
    service_manager.initializeServices(0);
    HAL_SetTick(1000);

    SUBCASE("ProcessRxQueue with Heartbeat Payloads - Stability Test 1")
    {
        constexpr uint8_t message[]{0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xA1, 0xE0};
        constexpr uint32_t EXT_CAN_ID = 0x107D552A;

        size_t stabilized = 0;

        for (uint32_t i = 0; i < 10; i++)
        {
            std::cout << "START Iteration " << i << ": allocated = " << o1heapGetDiagnostics(o1heap).allocated << std::endl;

            HAL_SetTick(i * 1000);

            CircularBuffer<HeaderFrame<8>, 4> rx_buffer;
            HeaderFrame<8>& frame = rx_buffer.begin_write();
            frame.extended_can_id = EXT_CAN_ID;
            frame.size = sizeof(message);
            memcpy(frame.payload, message, sizeof(message));
            frame.payload[7] += static_cast<uint8_t>(i);
            rx_buffer.commit_write();

            CHECK(rx_buffer.size() == 1);

            loop_manager.ProcessRxQueue(&canard_cyphal, &service_manager, empty_adapters, rx_buffer);
            service_manager.handleServices();
            CHECK(rx_buffer.size() == 0);

            size_t allocated = o1heapGetDiagnostics(o1heap).allocated;
            if (i == 1)
            {
                stabilized = allocated;
            }

            if (i > 1)
            {
                CHECK(allocated == stabilized);
            }

            std::cout << "END   Iteration " << i << ": allocated = " << o1heapGetDiagnostics(o1heap).allocated << std::endl;
        }
    }

}
