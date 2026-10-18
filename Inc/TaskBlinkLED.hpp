#pragma once

#include <stdint.h>
#include "Task.hpp"
#include "GpioPin.hpp"
#include "RegistrationManager.hpp"

// class RegistrationManager;

template<typename LEDPin>
class TaskBlinkLED : public Task {
public:
TaskBlinkLED() = delete;
TaskBlinkLED(uint32_t interval, uint32_t tick) : Task(interval, tick) {}

	virtual void handleMessage(std::shared_ptr<CyphalTransfer> /*transfer*/) override {};

	virtual void registerTask(RegistrationManager *manager, std::shared_ptr<Task> task) override;
	virtual void unregisterTask(RegistrationManager* manager, std::shared_ptr<Task> task) override;

    virtual void handleTaskImpl() override;

protected:
	LEDPin led_pin_;
};

template<typename LEDPin>
void TaskBlinkLED<LEDPin>::registerTask(RegistrationManager *manager, std::shared_ptr<Task> task)
{
	manager->subscribe(PURE_HANDLER, task);
}

template<typename LEDPin>
void TaskBlinkLED<LEDPin>::unregisterTask(RegistrationManager *manager, std::shared_ptr<Task> task)
{
	manager->unsubscribe(PURE_HANDLER, task);
}

template<typename LEDPin>
void TaskBlinkLED<LEDPin>::handleTaskImpl()
{
	led_pin_.toggle();
}
