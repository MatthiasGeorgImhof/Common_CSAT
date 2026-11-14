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

#include "cyphal.hpp"
#include "cyphal_mtu.hpp"
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
#include "TxQueueDrainer.hpp"
#include "RxQueueFiller.hpp"
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

#include <SocketConcept.hpp>
#include "Transport.hpp"
#include "GpioPin.hpp"
#include "SX126x.hpp"
#include "SX126xConfigurator.hpp"
#include "SX126xController.hpp"
#include "Socket.hpp"

#include <cppmain.h>
#include "Logger.hpp"

CanardAdapter ser_adapter;
CanardAdapter radio_adapter;
LoopardAdapter loop_adapter;

#ifndef CYPHAL_NODE_ID
#define CYPHAL_NODE_ID 21
#endif

constexpr CyphalNodeID cyphal_node_id = CYPHAL_NODE_ID;

constexpr size_t SER_RX_BUFFER_SIZE = 32;
using SerHeaderFrame = COBSHeaderFrame<MEDIUM_DATA_MTU>;
using SerCircularBuffer = CircularBuffer<SerHeaderFrame, SER_RX_BUFFER_SIZE>;
SerCircularBuffer ser_rx_buffer;

constexpr size_t SX_RX_BUFFER_SIZE = 8;
using SXHeaderFrame = HeaderFrame<LARGE_DATA_MTU>;
using SXCircularBuffer = CircularBuffer<SXHeaderFrame, SX_RX_BUFFER_SIZE>;
SXCircularBuffer sx_rx_buffer;

using CSPin    = GpioPin<GPIOA_BASE, SPI1_CS_Pin>;
using BusyPin  = GpioPin<GPIOA_BASE, SPI1_BUSY_Pin>;
using ResetPin = GpioPin<GPIOA_BASE, SPI1_RESET_Pin>;
using Dio1Pin  = GpioPin<GPIOA_BASE, SPI1_DIO1_Pin>;

using SpiConfig    = SPI_Stream_Config<hspi1, CSPin, 512>;
using SpiTransport = SPIStreamTransport<SpiConfig>;

using Radio = SX126x<SpiTransport, BusyPin, ResetPin, Dio1Pin>;
using Controller = SX126xController<Radio, SX126xRadioConfig, SXCircularBuffer>;



template<typename T, typename... Args>
static void register_task_with_heap(RegistrationManager& rm, Args&&... args)
{
    static SafeAllocator<T, LocalHeap> alloc;
    rm.add(alloc_unique_custom<T>(alloc, std::forward<Args>(args)...));
}

void print(const char* buffer)
{
    const size_t len = strlen(buffer);

    CDC_Transmit_FS(
        reinterpret_cast<uint8_t*>(const_cast<char*>(buffer)), len);
}

int format_frame(char* buffer, size_t buffer_size, const uint8_t* data, size_t size);

// 1. Raw continuous DMA buffer (size can be 256 or 512 bytes)
constexpr size_t DMA_RAW_BUF_SIZE = 512;
static uint8_t dma_raw_buf[DMA_RAW_BUF_SIZE];
static uint16_t dma_old_pos = 0;

// State machine for tracking frame byte index
static size_t frame_byte_idx = 0;

/**
 * Feeds incoming wire bytes one by one into the current slot of ser_rx_buffer.
 * Commits ONLY when the 0x00 end-of-frame delimiter is reached.
 */
static inline void process_rx_byte(uint8_t byte)
{
    auto& slot = ser_rx_buffer.begin_write();
    uint8_t* slot_raw = reinterpret_cast<uint8_t*>(&slot);

    if (byte == 0x00)
    {
        // 0x00 is a delimiter.
        // If we already collected data bytes, this 0x00 marks the END of the frame.
        if (frame_byte_idx > 1) // Must have at least start_of_frame (0x00) and some payload
        {
            // Store the end delimiter
            if (frame_byte_idx < sizeof(SerHeaderFrame))
            {
                slot_raw[frame_byte_idx] = 0x00;
            }

            // Successfully finished a frame! Commit to ring buffer for libcanard
            ser_rx_buffer.commit_write();
            frame_byte_idx = 0;
        }
        else
        {
            // Leading 0x00 or consecutive 0x00 delimiters: start of a new frame
            slot_raw[0] = 0x00;
            frame_byte_idx = 1;
        }
    }
    else
    {
        // Data byte
        if (frame_byte_idx > 0) // Only collect if we've seen a start delimiter
        {
            if (frame_byte_idx < sizeof(SerHeaderFrame))
            {
                slot_raw[frame_byte_idx++] = byte;
            }
            else
            {
                // Frame exceeded maximum slot MTU, discard corrupted frame
                frame_byte_idx = 0;
            }
        }
    }
}
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t pos)
{
    if (huart->Instance == USART1)
    {
        if (pos != dma_old_pos)
        {
            if (pos > dma_old_pos)
            {
                // Normal chunk: [dma_old_pos ... pos - 1]
                for (uint16_t i = dma_old_pos; i < pos; ++i)
                {
                    process_rx_byte(dma_raw_buf[i]);
                }
            }
            else
            {
                // Buffer wrap-around
                for (uint16_t i = dma_old_pos; i < DMA_RAW_BUF_SIZE; ++i)
                {
                    process_rx_byte(dma_raw_buf[i]);
                }
                for (uint16_t i = 0; i < pos; ++i)
                {
                    process_rx_byte(dma_raw_buf[i]);
                }
            }
            dma_old_pos = pos;
        }

        HAL_GPIO_TogglePin(GPIOC, LED_Pin);
    }
}

void cppmain()
{
	HAL_Delay(5000);
	print("start cppmain\n");

	LocalHeap::initialize();

	using LoopardCyphal = Cyphal<LoopardAdapter>;
	loop_adapter.memory_allocate = LocalHeap::loopardMemoryAllocate;
	loop_adapter.memory_free = LocalHeap::loopardMemoryDeallocate;
	LoopardCyphal loop_cyphal(&loop_adapter);
	loop_cyphal.setNodeID(cyphal_node_id);

	using CanardCyphal = Cyphal<CanardAdapter>;
	CanardMemoryResource canard_memory = {nullptr, LocalHeap::canardMemoryDeallocate, LocalHeap::canardMemoryAllocate };

	ser_adapter.ins = canardInit(canard_memory);
	ser_adapter.que = canardTxInit(128, SMALL_DATA_MTU, canard_memory);
	CanardCyphal ser_cyphal(&ser_adapter);
	ser_cyphal.setNodeID(cyphal_node_id);
	ser_adapter.ins.forward_start_id = 1;
	ser_adapter.ins.forward_end_id = 127;

	radio_adapter.ins = canardInit(canard_memory);
	radio_adapter.que = canardTxInit(16, LARGE_DATA_MTU, canard_memory);
	CanardCyphal radio_cyphal(&radio_adapter);
	radio_cyphal.setNodeID(cyphal_node_id);
	radio_adapter.ins.forward_start_id = 1;
	radio_adapter.ins.forward_end_id = 127;

	std::tuple<Cyphal<CanardAdapter>, Cyphal<CanardAdapter>> serradio_adapters = { ser_cyphal, radio_cyphal };
	std::tuple<Cyphal<CanardAdapter>> radio_adapters = { radio_cyphal };
	std::tuple<Cyphal<CanardAdapter>> ser_adapters = { ser_cyphal };
	std::tuple<> empty_adapters = {} ;

    SpiConfig config;
    SpiTransport transport(config);
    Radio radio(transport);
    print("radio reset\n");
    radio.Reset();

    SX126xRadioConfig radio_config {
        .packet_type = sx126x::PacketType::LoRa,
        .frequency_hz = 917000000,
        .pa_config = sx126x::PaConfig {
        	.paDutyCycle = 0x04,
        	.hpMax = 0x07,
			.deviceSel = 0x00,
			.paLut = 0x01
    	},
        .power_dbm = 0,
        .modulation = sx126x::LoRaModulationParams {
			.sf = 7 /*SF7*/,
			.bw = 4 /*BW125kHz*/,
			.cr = 1 /*CR4/5*/,
			.ldro = 0
		},
        .packet = sx126x::LoRaPacketParams {
			.preamble_length = 8 /*preamble*/,
			.header_type = sx126x::HeaderType::Variable /*explicit header*/,
			.payload_length = 255 /*payload length*/,
			.crc = 1 /*crc on*/,
			.iq = 0
		},
		.irqs = sx126x::IrqMasks {
			.IrqMask = sx126x::IRQ::TxDone | sx126x::IRQ::RxDone | sx126x::IRQ::Timeout | sx126x::IRQ::CrcErr,
			.Dio1Mask = sx126x::IRQ::TxDone | sx126x::IRQ::RxDone | sx126x::IRQ::Timeout | sx126x::IRQ::CrcErr,
			.Dio2Mask = sx126x::IRQ::Timeout,
			.Dio3Mask = sx126x::IRQ::CrcErr

		},
        .rf_switch_enable = true,
        .tcxo_enable = true,
        .tcxo_voltage = 0x07,
        .tcxo_timeout_ticks = 320
    };

    SX126xConfigurator radio_configurator(radio, radio_config);
    radio_configurator.configure();

    Controller controller(radio, radio_config, sx_rx_buffer);
	controller.start();

	RegistrationManager registration_manager;
	SubscriptionManager subscription_manager;

	LoopManager<LocalHeap> loop_manager;

	using TSHeart = TaskSendHeartBeat<CanardCyphal, CanardCyphal>;
	register_task_with_heap<TSHeart>(registration_manager, 2000, 100, 0, serradio_adapters);

	using TPHeart = TaskProcessHeartBeat<CanardCyphal, CanardCyphal>;
	register_task_with_heap<TPHeart>(registration_manager, 2000, 100, serradio_adapters);

	constexpr uint8_t uuid[] = {0xc4, 0xd1, 0x98, 0x0f, 0x12, 0x75, 0x4d, 0xf4, 0xb8, 0x4e, 0x12, 0xbf, 0x5a, 0xf2, 0x1f, 0x75};
	constexpr char node_name[50] = "WeActF11_W5500_Canard";
	using TRespondInfo = TaskRespondGetInfo<CanardCyphal, CanardCyphal>;
	register_task_with_heap<TRespondInfo>(registration_manager, uuid, node_name, 10000, 100, serradio_adapters);

//	using LEDPin  = GpioPin<GPIOC_BASE, GPIO_PIN_13>;
//	using TBlink = TaskBlinkLED<LEDPin>;
//	register_task_with_heap<TBlink>(registration_manager, 1000, 100);

	using TCheckMem = TaskCheckMemory;
	O1HeapInstance *o1heap = LocalHeap::getO1Heap();
	register_task_with_heap<TCheckMem>(registration_manager, o1heap, 250, 100);

	subscription_manager.subscribeAll(registration_manager, ser_adapters);
	subscription_manager.subscribe<SubscriptionManager::MessageTag>(static_cast<CyphalPortID>(uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_), ser_adapters);
//	subscription_manager.subscribe<SubscriptionManager::MessageTag>(static_cast<CyphalPortID>(uavcan_node_port_List_1_0_FIXED_PORT_ID_), ser_adapters);
//	subscription_manager.subscribe<SubscriptionManager::MessageTag>(static_cast<CyphalPortID>(uavcan_diagnostic_Record_1_1_FIXED_PORT_ID_), ser_adapters);
//
//	subscription_manager.subscribe<SubscriptionManager::RequestTag>(static_cast<CyphalPortID>(uavcan_node_GetInfo_1_0_FIXED_PORT_ID_), ser_adapters);
//	subscription_manager.subscribe<SubscriptionManager::RequestTag>(static_cast<CyphalPortID>(uavcan_file_Write_1_1_FIXED_PORT_ID_), ser_adapters);
//	subscription_manager.subscribe<SubscriptionManager::RequestTag>(static_cast<CyphalPortID>(uavcan_file_Read_1_1_FIXED_PORT_ID_), ser_adapters);
//
//	subscription_manager.subscribe<SubscriptionManager::ResponseTag>(static_cast<CyphalPortID>(uavcan_node_GetInfo_1_0_FIXED_PORT_ID_), ser_adapters);
//	subscription_manager.subscribe<SubscriptionManager::ResponseTag>(static_cast<CyphalPortID>(uavcan_file_Write_1_1_FIXED_PORT_ID_), ser_adapters);
//	subscription_manager.subscribe<SubscriptionManager::ResponseTag>(static_cast<CyphalPortID>(uavcan_file_Read_1_1_FIXED_PORT_ID_), ser_adapters);

    ServiceManager service_manager(registration_manager.getHandlers());
	service_manager.initializeServices(HAL_GetTick());

	SX126xTxQueueDrainer<Controller, LARGE_DATA_MTU> sx126x_drainer(&radio_adapter, &controller);
	SerialTxQueueDrainer<MEDIUM_DATA_MTU> ser_drainer(&ser_adapter, &huart1);

	HAL_UARTEx_ReceiveToIdle_DMA(&huart1, dma_raw_buf, DMA_RAW_BUF_SIZE);
	__HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT); // Disable half-transfer interrupt


    while(1)
	{
		auto irq = radio.GetIrqStatus();
		if (irq != sx126x::IRQ::None)
			controller.notify_irq();
		controller.process();

		loop_manager.ProcessRxQueue(&ser_cyphal, &service_manager, radio_adapters, ser_rx_buffer);
		loop_manager.ProcessRxQueue(&radio_cyphal, &service_manager, ser_adapters, sx_rx_buffer);

//		char buffer[256];
//		snprintf(buffer, sizeof(buffer), "ser_rx_buffer %3d of %3d; rad_rx_buffer %3d of %3d",
//				ser_rx_buffer.size(), ser_rx_buffer.capacity(), rad_rx_buffer.size(), rad_rx_buffer.capacity());
//		log(LOG_LEVEL_DEBUG, "Superloop: %s\r\n", buffer);

		sx126x_drainer.drain();
		ser_drainer.drain();
		loop_manager.LoopProcessRxQueue(&loop_cyphal, &service_manager, empty_adapters);
		service_manager.handleServices();
	}
}
