#pragma once

#ifdef __arm__
#include "stm32xxxx.h"
#else
#include "mock_hal.h"
#endif

#if !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)

// Forward declaration only
struct CanardAdapter;

class CanTxQueueDrainer
{
public:
    CanTxQueueDrainer(CanardAdapter* adapter, CAN_HandleTypeDef* hcan);

    void drain();
    void irq_safe_drain();

private:
    CanardAdapter* adapter_;
    CAN_HandleTypeDef* hcan_;
};
#endif // !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)
