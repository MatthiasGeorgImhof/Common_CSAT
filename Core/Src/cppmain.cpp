// AUX

#include <cppmain.h>
#include <cpphal.h>
#include "usb_device.h"

#include <memory>

#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "usbd_cdc_if.h"

#include <main.h>
#include <cppmain.h>
#include "Transport.hpp"
#include "GpioPin.hpp"
#include "SX126x.hpp"

using CSPin    = GpioPin<GPIOA_BASE, SPI1_CS_Pin>;
using BusyPin  = GpioPin<GPIOA_BASE, SPI1_BUSY_Pin>;
using ResetPin = GpioPin<GPIOA_BASE, SPI1_RESET_Pin>;
using Dio1Pin  = GpioPin<GPIOA_BASE, SPI1_DIO1_Pin>;

using SpiConfig    = SPI_Stream_Config<hspi1, CSPin, 512>;
using SpiTransport = SPIStreamTransport<SpiConfig>;

using Radio = SX126x<SpiTransport, BusyPin, ResetPin, Dio1Pin>;


void print(const char* buffer)
{
    const size_t len = strlen(buffer);

    CDC_Transmit_FS(reinterpret_cast<uint8_t*>(const_cast<char*>(buffer)), len);
}

void cppmain()
{
	HAL_Delay(5000);
	print("start cppmain\n");

    SpiConfig config;
    SpiTransport transport(config);

    Radio radio(transport);
    print("Reset\n");
    radio.Reset();

    print("Init\n");
    radio.Init();

    // If your module has a TCXO (e.g. 3.3V TCXO with 5ms startup delay):
    radio.SetDio3AsTcxoCtrl(0x07, 320);
    radio.ClearDeviceErrors(); // Clear power-on transient error

    // Enable DIO2 RF switch control (standard on almost all modules)
    radio.SetDio2AsRfSwitchCtrl(true);

    radio.SetPacketType(sx126x::PacketType::LoRa);
    radio.SetFrequency(916500000);

    // --- MANDATORY TX CONFIGURATION ---
    // Configuration for SX1262 (+22dBm capable PA):
    constexpr static struct sx126x::PaConfig paconfig = {0x04, 0x07, 0x00, 0x01};

    radio.SetPaConfig(paconfig);

    // Set TX Power to +14 dBm, 200us ramp time
    radio.SetTxPower(0);

    radio.SetBufferBaseAddress(0x00, 0x80);

    constexpr static struct sx126x::LoRaModulationParams modulation_params =
    {
        7,  // SF7
        4,  // BW125kHz
        1,  // CR4/5
        0
    };

    radio.SetModulationParams(modulation_params);

    constexpr static struct sx126x::LoRaPacketParams packet_params =
    {
        8,      // preamble
        0,      // explicit header
        15,     // payload length
        1,      // crc on
        0
    };

    radio.SetPacketParams(packet_params);

//    constexpr static struct sx126x::IrqMasks irq_masks =
//    {
//    		sx126x::IRQ::TxDone,
//			sx126x::IRQ::TxDone,
//			sx126x::IRQ::None,
//			sx126x::IRQ::None
//    };

    constexpr uint16_t irq_all =
            static_cast<uint16_t>(sx126x::IRQ::TxDone)  |
            static_cast<uint16_t>(sx126x::IRQ::RxDone)  |
            static_cast<uint16_t>(sx126x::IRQ::Timeout) |
            static_cast<uint16_t>(sx126x::IRQ::CrcErr);

	constexpr static struct sx126x::IrqMasks irq_masks =
	{
		static_cast<sx126x::IRQ>(irq_all),
		static_cast<sx126x::IRQ>(irq_all), // Route to DIO1
		sx126x::IRQ::None,
		sx126x::IRQ::None
	};

	radio.SetDioIrqParams(irq_masks);

    auto status = radio.GetStatus();
    auto error = radio.GetDeviceErrors();
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "status %2x error %2x \n", status, error);
	print(buffer);

//	print("enter loop\n");
//	auto tick = HAL_GetTick();

//	while (1)
//	{
//		if ((HAL_GetTick() - tick) > 2000)
//		{
//			tick = HAL_GetTick();
//			HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
//
//			auto res1 = radio.GetStatus();
//			auto irq1 = radio.GetIrqStatus();
//		    auto error1 = radio.GetDeviceErrors();
//
//			uint8_t msg[] = "Hello";
//			radio.WriteBuffer(0, msg, 5);
//
//			radio.StartTx();
//			uint32_t start = HAL_GetTick();
//			while (!radio.IsIRQAsserted() && (HAL_GetTick() - start < 500))
//			{
//			    // wait for transmission to finish
//			}
//
//			auto res2 = radio.GetStatus();
//			auto irq2 = radio.GetIrqStatus();
//		    auto error2 = radio.GetDeviceErrors();
//
//			snprintf(buffer, sizeof(buffer),
//					"before status %2x irq %4x error %2x; after status %2x irq %4x error %2x \n",
//					res1, irq1, error1, res2, irq2, error2);
//			print(buffer);
//
//			radio.ClearIrqStatus(irq2);
//		}
//
//	}


//	uint32_t uid;
//	uid = HAL_GetUIDw0();
//	while (1)
//	{
//		if ((HAL_GetTick() - tick) > 2000)
//		{
//			tick = HAL_GetTick();
//			HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
//
//			// ------------------------------------------------------------
//			// 1. TRANSMIT
//			// ------------------------------------------------------------
//			char msg[32];
//			snprintf(msg, sizeof(msg), "Hello from %4lx", uid);
//			radio.WriteBuffer(0x00, reinterpret_cast<uint8_t*>(msg), 15);
//			radio.StartTx();
//
//			// Wait for TX to complete (or 500 ms timeout)
//			uint32_t start = HAL_GetTick();
//			while (!radio.IsIRQAsserted() && (HAL_GetTick() - start < 500)) {}
//
//			uint16_t tx_irq = radio.GetIrqStatus();
//			radio.ClearIrqStatus(tx_irq);
//
//			print("TX Done\n");
//
//			// ------------------------------------------------------------
//			// 2. RECEIVE (Wait up to 1000 ms for a reply)
//			// ------------------------------------------------------------
//			// Start listening (0xFFFFFF = continuous, or stops on packet received)
//			radio.StartRx();
//
//			start = HAL_GetTick();
//			while (!radio.IsIRQAsserted() && (HAL_GetTick() - start < 1000))
//			{
//				// Waiting for incoming packet or timeout
//			}
//
//			uint16_t rx_irq = radio.GetIrqStatus();
//			radio.ClearIrqStatus(rx_irq);
//
//			// Put back into Standby mode once done listening
//			radio.SetStandby();
//
//			// ------------------------------------------------------------
//			// 3. PROCESS RECEIVED PACKET
//			// ------------------------------------------------------------
//			if (rx_irq & static_cast<uint16_t>(sx126x::IRQ::RxDone))
//			{
//				if (rx_irq & static_cast<uint16_t>(sx126x::IRQ::CrcErr))
//				{
//					print("Packet received but CRC error!\n");
//				}
//				else
//				{
//					// Query FIFO location and length
//					auto rx_info = radio.GetRxBufferStatus();
//
//					uint8_t rx_payload[256] = {0};
//					radio.ReadBuffer(rx_info.buffer_pointer, rx_payload, rx_info.payload_length);
//					rx_payload[rx_info.payload_length] = '\0'; // null-terminate string
//
//					auto pkt_status = radio.GetPacketStatus();
//
//					char rx_msg[1024];
//					snprintf(rx_msg, sizeof(rx_msg),
//							 "RX [%d bytes]: '%s' | RSSI: %d dBm | SNR: %d dB\n",
//							 rx_info.payload_length, rx_payload,
//							 pkt_status.rssi_dbm, pkt_status.snr_db);
//					print(rx_msg);
//				}
//			}
//			else
//			{
//				print("RX Timeout (no packet)\n");
//			}
//		}
	// Put radio into Continuous RX right from the start

	radio.StartRx();

	print("enter loop\n");
	auto tick = HAL_GetTick();
	uint32_t uid = HAL_GetUIDw0();

	// Use a small random delay/jitter so the two nodes don't collide
	uint32_t tx_interval = 2000 + (uid % 500);

	while (1)
	{
		// ------------------------------------------------------------
		// 1. CHECK FOR INCOMING PACKET (Asynchronous / Continuous)
		// ------------------------------------------------------------
		if (radio.IsIRQAsserted())
		{
			uint16_t irq = radio.GetIrqStatus();
			radio.ClearIrqStatus(irq);

			if (irq & static_cast<uint16_t>(sx126x::IRQ::RxDone))
			{
				if (irq & static_cast<uint16_t>(sx126x::IRQ::CrcErr))
				{
					print("CRC Error!\n");
				}
				else
				{
					auto rx_info = radio.GetRxBufferStatus();
					uint8_t rx_payload[256] = {0};
					radio.ReadBuffer(rx_info.buffer_pointer, rx_payload, rx_info.payload_length);
					rx_payload[rx_info.payload_length] = '\0';

					auto pkt_status = radio.GetPacketStatus();

					char rx_msg[1024];
					snprintf(rx_msg, sizeof(rx_msg),
							 "RX [%d bytes]: '%s' | RSSI: %d dBm | SNR: %d dB\n",
							 rx_info.payload_length, rx_payload,
							 pkt_status.rssi_dbm, pkt_status.snr_db);
					print(rx_msg);
				}
			}

			// Ensure radio stays in RX mode
			radio.StartRx();
		}

		// ------------------------------------------------------------
		// 2. PERIODIC TRANSMIT
		// ------------------------------------------------------------
		if ((HAL_GetTick() - tick) > tx_interval)
		{
			tick = HAL_GetTick();
			HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

			// Transmit packet
			char msg[32];
			snprintf(msg, sizeof(msg), "Hello from %04lX", uid & 0xFFFF);
			radio.WriteBuffer(0x00, reinterpret_cast<uint8_t*>(msg), 15);

			radio.StartTx();

			// Wait for TX completion (~40 ms)
			uint32_t start = HAL_GetTick();
			while (!radio.IsIRQAsserted() && (HAL_GetTick() - start < 200)) {}

			uint16_t tx_irq = radio.GetIrqStatus();
			radio.ClearIrqStatus(tx_irq);

			print("TX Done\n");

			// IMMEDIATELY resume listening!
			radio.StartRx();
		}
	}
}
