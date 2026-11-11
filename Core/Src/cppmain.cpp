// AUX

#include <cppmain.h>
#include <cpphal.h>
#include "usb_device.h"

#include <memory>

#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "usbd_cdc_if.h"

#include <cppmain.h>
#include <SocketConcept.hpp>
#include "Transport.hpp"
#include "GpioPin.hpp"

void cppmain()
{
	HAL_Delay(5000);
	print("start cppmain\n");

	ethernet.initialize();
	udp_socket.open(12345);


	auto tick = HAL_GetTick();
	while (1)
	{
		if ((HAL_GetTick() - tick) > 2000)
		{
			tick = HAL_GetTick();
			HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

			char payload[] = "hello from stm32f411\n";
			udp_socket.send(reinterpret_cast<const uint8_t*>(payload), sizeof(payload), destination);

		}
		if (udp_socket.poll())
		{
			Endpoint source;
			uint8_t buf[128];
			udp_socket.receive(buf, sizeof(buf), source);
			print(reinterpret_cast<const char*>(buf));

		}

	}
}
