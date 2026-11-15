#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "mock_hal.h"
#include "TaskBlinkLED.hpp"
#include "RegistrationManager.hpp"
#include <memory>

static constexpr uint32_t GPIOC_BASE = static_cast<uint32_t>(0xDEADBEEF); // value irrelevant for mock 

GPIO_TypeDef GPIOx;

using LEDPin  = GpioPin<GPIOC_BASE, GPIO_PIN_5>;
using TBlink = TaskBlinkLED<LEDPin>;
    
TEST_CASE("TaskBlinkLED::registerTask adds task to RegistrationManager") {
    // Create instances of TaskBlinkLED and RegistrationManager
    uint32_t interval = 100;
    uint32_t tick = 0;
    auto task = std::make_shared<TBlink>(interval, tick);
    RegistrationManager manager;

    // Call registerTask
    task->registerTask(&manager, task);

    // Verify that the task is added to the RegistrationManager
    CHECK(manager.containsTask(task));
}

TEST_CASE("TaskBlinkLED::unregisterTask removes task from RegistrationManager") {
    // Create instances of TaskBlinkLED and RegistrationManager
    uint32_t interval = 100;
    uint32_t tick = 0;
    auto task = std::make_shared<TBlink>(interval, tick);
    RegistrationManager manager;

    // First, add the task to the RegistrationManager
    manager.add(task);
    CHECK(manager.containsTask(task));

    // Then, call unregisterTask
    task->unregisterTask(&manager, task);

    // Verify that the task is removed from the RegistrationManager
    CHECK(!manager.containsTask(task));
}

TEST_CASE("TaskBlinkLED::handleTaskImpl toggles GPIO Pin") {
    // Create an instance of TaskBlinkLED
    uint32_t interval = 100;
    uint32_t tick = 0;
    auto task = std::make_shared<TBlink>(interval, tick);

    // Ensure the pin is initially reset
    HAL_GPIO_WritePin(&GPIOx, GPIO_PIN_5, GPIO_PIN_RESET);
    CHECK(HAL_GPIO_ReadPin(&GPIOx, GPIO_PIN_5) == GPIO_PIN_RESET);

    // Execute the task, which should toggle the pin
    task->handleTaskImpl();
    CHECK(HAL_GPIO_ReadPin(&GPIOx, GPIO_PIN_5) == GPIO_PIN_SET);

    // Execute the task again, which should toggle the pin back
    task->handleTaskImpl();
    CHECK(HAL_GPIO_ReadPin(&GPIOx, GPIO_PIN_5) == GPIO_PIN_RESET);
}

TEST_CASE("Task::handleTask execution test") {
    // Create instances of TaskBlinkLED
    uint32_t interval = 100;
    uint32_t tick = 0;
    auto task = std::make_shared<TBlink>(interval, tick);

    //Initialize the task
    task->initialize(0);
    CHECK(task->getLastTick() == tick + task->getShift());

    //Set tick such that we pass the check
    HAL_SetTick(interval + task->getLastTick() + 1);
    task->handleTask();
    CHECK(HAL_GPIO_ReadPin(&GPIOx, GPIO_PIN_5) == GPIO_PIN_SET);

     //Set tick such that we don't pass the check
    HAL_SetTick(interval + task->getLastTick() - 1);
    task->handleTask();
    CHECK(HAL_GPIO_ReadPin(&GPIOx, GPIO_PIN_5) == GPIO_PIN_SET);

}