#pragma once

#if defined(STM32L4xx)
#include "stm32l4xx.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_rcc.h"
#include "stm32l4xx_hal_rtc.h"

#elif defined(STM32F4xx)
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_rtc.h"

#elif defined(STM32H4xx)
#include "stm32h4xx.h"
#include "stm32h4xx_hal.h"
#include "stm32h4xx_hal_rcc.h"
#include "stm32h4xx_hal_rtc.h"
#endif

#include "usbd_cdc_if.h"
#include "usb_device.h"
