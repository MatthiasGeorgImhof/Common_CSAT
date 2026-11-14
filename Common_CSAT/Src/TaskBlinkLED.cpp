#include "TaskBlinkLED.hpp"
#include "RegistrationManager.hpp"
#include "cyphal.hpp"

#ifdef __arm__
#include "usbd_cdc_if.h"
#endif
#ifdef __x86_64__
#include "mock_hal.h"
#endif

// template<typename LEDPin>
// void TaskBlinkLED<LEDPin>::registerTask(RegistrationManager *manager, std::shared_ptr<Task> task)
// {
// 	manager->subscribe(PURE_HANDLER, task);
// }

// template<typename LEDPin>
// void TaskBlinkLED<LEDPin>::unregisterTask(RegistrationManager *manager, std::shared_ptr<Task> task)
// {
// 	manager->unsubscribe(PURE_HANDLER, task);
// }

// template<typename LEDPin>
// void TaskBlinkLED<LEDPin>::handleTaskImpl()
// {
// 	led_pin_.toggle();
// }
