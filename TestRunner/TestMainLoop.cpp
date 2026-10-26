#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "mock_hal.h"

#include "o1heap.h"
#include "HeapAllocation.hpp"
#include "Logger.hpp"
#include "cyphal.hpp"
#include "canard_adapter.hpp"
#include "loopard_adapter.hpp"
#include "RegistrationManager.hpp"
#include "ServiceManager.hpp"
#include "SubscriptionManager.hpp"
#include "ProcessRxQueue.hpp"
#include "GpioPin.hpp"
#include "Task.hpp"
#include "TaskCheckMemory.hpp"
#include "TaskBlinkLED.hpp"
#include "TaskSendHeartBeat.hpp"

CanardAdapter canard_adapter;
LoopardAdapter loopard_adapter;
CanardAdapter serial_adapter;

constexpr size_t O1HEAP_SIZE = 16384;
uint8_t o1heap_buffer[O1HEAP_SIZE] __attribute__((aligned(O1HEAP_ALIGNMENT)));
O1HeapInstance *o1heap;

struct TestLocalHeap
{
    static void *heapAllocate(void * /*handle*/, const size_t amount)
    {
        return o1heapAllocate(o1heap, amount);
    }

    static void heapFree(void * /*handle*/, void *pointer)
    {
        o1heapFree(o1heap, pointer);
    }
};

template <typename T, typename... Args>
static void register_task_with_heap(RegistrationManager &rm, Args &&...args)
{
    static SafeAllocator<T, TestLocalHeap> alloc;
    rm.add(alloc_unique_custom<T>(alloc, std::forward<Args>(args)...));
}
using CanardCyphal = Cyphal<CanardAdapter>;
using SerardCyphal = Cyphal<CanardAdapter>;

constexpr CyphalNodeID cyphal_node_id = 11;
UART_HandleTypeDef *huart2_;
UART_HandleTypeDef *huart3_;
CAN_HandleTypeDef *hcan_;

constexpr uint32_t SERIAL_TIMEOUT = 1000;
constexpr size_t SERIAL_BUFFER_SIZE = 4;
using SerialCircularBuffer = CircularBuffer<SerialFrame, SERIAL_BUFFER_SIZE>;
SerialCircularBuffer serial_buffer;

void *canardMemoryAllocate(void *const /*canard*/, const size_t size)
{
    return o1heapAllocate(o1heap, size);
}

void canardMemoryDeallocate(void *const /*canard*/, const size_t /*size*/, void *const pointer)
{
    o1heapFree(o1heap, pointer);
}

GPIO_TypeDef *GPIOC = nullptr;

#define AVI_RST_Pin GPIO_PIN_0
#define AVI_RST_GPIO_Port GPIOC
#define SCI_RST_Pin GPIO_PIN_1
#define SCI_RST_GPIO_Port GPIOC
#define COMM1_RST_Pin GPIO_PIN_2
#define COMM1_RST_GPIO_Port GPIOC
#define COMM2_RST_Pin GPIO_PIN_3
#define COMM2_RST_GPIO_Port GPIOC
#define ATTENTION_Pin GPIO_PIN_4
#define ATTENTION_GPIO_Port GPIOA
#define EPS_RST_Pin GPIO_PIN_5
#define EPS_RST_GPIO_Port GPIOA
#define LED1_Pin GPIO_PIN_6
#define LED1_GPIO_Port GPIOC
#define LED2_Pin GPIO_PIN_7
#define LED2_GPIO_Port GPIOC
#define LED3_Pin GPIO_PIN_8
#define LED3_GPIO_Port GPIOC
#define LED4_Pin GPIO_PIN_9
#define LED4_GPIO_Port GPIOC
#define LED5_Pin GPIO_PIN_8
#define LED5_GPIO_Port GPIOA

static constexpr uint32_t GPIOA_BASE = static_cast<uint32_t>(0xDEADBEEF); // value irrelevant for mock 
static constexpr uint32_t GPIOB_BASE = static_cast<uint32_t>(0xCAFECAFE); // value irrelevant for mock 
static constexpr uint32_t GPIOC_BASE = static_cast<uint32_t>(0xAABBCCDD); // value irrelevant for mock 

struct CanardMemoryResource canard_memory = { nullptr, canardMemoryDeallocate, canardMemoryAllocate};
CircularBuffer<HeaderFrame<8>, 4> rx_buffer;

TEST_CASE("TaskMainLoop Serial: TaskSendHeartBeat TaskBlinkLED TaskCheckMemory")
{
    constexpr uint8_t SERIAL_MTU = 16;
    o1heap = o1heapInit(o1heap_buffer, O1HEAP_SIZE);
    CHECK(o1heap != nullptr);

    LoopardAdapter loopard_adapter;
    Cyphal<LoopardAdapter> loopard_cyphal(&loopard_adapter);
    loopard_cyphal.setNodeID(cyphal_node_id);

    serial_adapter.ins = canardInit(canard_memory);
    serial_adapter.que = canardTxInit(16, SERIAL_MTU, canard_memory);
    Cyphal<CanardAdapter> serial_cyphal(&serial_adapter);
    serial_cyphal.setNodeID(cyphal_node_id);

    std::tuple<Cyphal<CanardAdapter>> adapters = {serial_cyphal};

    RegistrationManager registration_manager;

    using TSHeartSerard = TaskSendHeartBeat<SerardCyphal>;
    register_task_with_heap<TSHeartSerard>(registration_manager, 1000U, 100U, static_cast<CyphalTransferID>(0), adapters);

    using LEDPin  = GpioPin<GPIOC_BASE, GPIO_PIN_5>;
    using TBlink = TaskBlinkLED<LEDPin>;
    register_task_with_heap<TBlink>(registration_manager, 1000U, 100U);

    using TCheckMem = TaskCheckMemory;
    register_task_with_heap<TCheckMem>(registration_manager, o1heap, 2000U, 100U);

    REQUIRE(registration_manager.getHandlers().size() == 3);

    ServiceManager service_manager(registration_manager.getHandlers());
    SubscriptionManager subscription_manager;
    subscription_manager.subscribe<SubscriptionManager::MessageTag>(registration_manager.getSubscriptions(), adapters);
    subscription_manager.subscribe<SubscriptionManager::RequestTag>(registration_manager.getServers(), adapters);
    subscription_manager.subscribe<SubscriptionManager::ResponseTag>(registration_manager.getClients(), adapters);

    LoopManager<TestLocalHeap> loop_manager;

    O1HeapDiagnostics diagnostic_before = o1heapGetDiagnostics(o1heap);
    clear_uart_tx_buffer();

    REQUIRE(service_manager.getHandlers().size() == 3);
    REQUIRE(subscription_manager.getSubscriptions().size() == 0);

    SerialTxQueueDrainer<SERIAL_MTU> serial_drainer(&serial_adapter, huart2_);
    for (uint32_t tick = 3000; tick <= 90000; tick += 3000)
    {
        HAL_SetTick(tick);
        log(LOG_LEVEL_TRACE, "while loop: %d\r\n", HAL_GetTick());
        loop_manager.ProcessRxQueue(&serial_cyphal, &service_manager, adapters, rx_buffer);
        loop_manager.LoopProcessRxQueue(&loopard_cyphal, &service_manager, adapters);
        service_manager.handleServices();
        serial_drainer.drain();

        CHECK(get_uart_tx_buffer_count() == sizeof(COBSHeaderFrame<SERIAL_MTU>));
        clear_uart_tx_buffer();
    }
    O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);
    CHECK(diagnostic_before.allocated == diagnostic_after.allocated);
}

TEST_CASE("TaskMainLoop CAN: TaskSendHeartBeat TaskBlinkLED TaskCheckMemory")
{
    o1heap = o1heapInit(o1heap_buffer, O1HEAP_SIZE);
    CHECK(o1heap != nullptr);

    LoopardAdapter loopard_adapter;
    Cyphal<LoopardAdapter> loopard_cyphal(&loopard_adapter);
    loopard_cyphal.setNodeID(cyphal_node_id);

    canard_adapter.ins = canardInit(canard_memory);
    canard_adapter.que = canardTxInit(16, CANARD_MTU_CAN_CLASSIC, canard_memory);
    Cyphal<CanardAdapter> canard_cyphal(&canard_adapter);
    canard_cyphal.setNodeID(cyphal_node_id);

    std::tuple<Cyphal<CanardAdapter>> adapters = {canard_cyphal};

    RegistrationManager registration_manager;

    using TSHeartSerard = TaskSendHeartBeat<SerardCyphal>;
    register_task_with_heap<TSHeartSerard>(registration_manager, 1000U, 100U, static_cast<CyphalTransferID>(0), adapters);

    using LEDPin  = GpioPin<GPIOC_BASE, GPIO_PIN_5>;
    using TBlink = TaskBlinkLED<LEDPin>;
    register_task_with_heap<TBlink>(registration_manager, 1000U, 100U);

    using TCheckMem = TaskCheckMemory;
    register_task_with_heap<TCheckMem>(registration_manager, o1heap, 2000U, 100U);

    REQUIRE(registration_manager.getHandlers().size() == 3);

    ServiceManager service_manager(registration_manager.getHandlers());
    SubscriptionManager subscription_manager;
    subscription_manager.subscribe<SubscriptionManager::MessageTag>(registration_manager.getSubscriptions(), adapters);
    subscription_manager.subscribe<SubscriptionManager::RequestTag>(registration_manager.getServers(), adapters);
    subscription_manager.subscribe<SubscriptionManager::ResponseTag>(registration_manager.getClients(), adapters);

    LoopManager<TestLocalHeap> loop_manager;

    O1HeapDiagnostics diagnostic_before = o1heapGetDiagnostics(o1heap);
    clear_can_tx_buffer();

    REQUIRE(service_manager.getHandlers().size() == 3);
    REQUIRE(subscription_manager.getSubscriptions().size() == 0);

    CanTxQueueDrainer can_drainer(&canard_adapter, hcan_);
    for (uint32_t tick = 3000; tick <= 90000; tick += 3000)
    {
        HAL_SetTick(tick);
        log(LOG_LEVEL_TRACE, "while loop: %d\r\n", HAL_GetTick());
        loop_manager.ProcessRxQueue(&canard_cyphal, &service_manager, adapters, rx_buffer);
        loop_manager.LoopProcessRxQueue(&loopard_cyphal, &service_manager, adapters);
        service_manager.handleServices();
        can_drainer.drain();

        CHECK(get_can_tx_buffer_count() == 1);
        clear_can_tx_buffer();
    }
    O1HeapDiagnostics diagnostic_after = o1heapGetDiagnostics(o1heap);
    CHECK(diagnostic_before.allocated == diagnostic_after.allocated);
}