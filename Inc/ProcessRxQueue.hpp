#ifndef RX_PROCESSING_HPP
#define RX_PROCESSING_HPP

#include <tuple>
#include <memory>

#include "cyphal.hpp"
#include "canard_adapter.hpp"
#include "serard_adapter.hpp"
#include "loopard_adapter.hpp"

#include "CircularBuffer.hpp"
#include "ServiceManager.hpp"
#include "o1heap.h"
#include "Logger.hpp"
#include "CanTxQueueDrainer.hpp"

using LocalHeap = HeapAllocation<65536>;

#if defined(HAL_CAN_MODULE_ENABLED) || defined(MOCK_HAL_CAN_ENABLED)
extern CanTxQueueDrainer tx_drainer;
#endif // defined(HAL_CAN_MODULE_ENABLED) || defined(MOCK_HAL_CAN_ENABLED)

constexpr size_t SERIAL_MTU = 640;
struct SerialFrame
{
    size_t size;
    uint8_t data[SERIAL_MTU];
};

#if defined(HAL_CAN_MODULE_ENABLED) || defined(MOCK_HAL_CAN_ENABLED)
constexpr size_t CAN_MTU = 8;
struct CanRxFrame
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[CAN_MTU];
};
#endif // defined(HAL_CAN_MODULE_ENABLED) || defined(MOCK_HAL_CAN_ENABLED)

template <typename Heap>
class LoopManager
{
public:
    LoopManager() = default;

    // Common transfer processing function
    template <typename... Adapters>
    bool processTransfer(CyphalTransfer &transfer, ServiceManager *service_manager, std::tuple<Adapters...> &adapters)
    {
//    	constexpr size_t BUFFER_SIZE = 512;
//    	char hex_string_buffer[BUFFER_SIZE];
//    	uchar_buffer_to_hex(static_cast<const unsigned char*>(transfer.payload), transfer.payload_size, hex_string_buffer, BUFFER_SIZE);
//        log(LOG_LEVEL_DEBUG, "LoopManager::processTransfer: %4d %s\r\n", transfer.metadata.port_id, hex_string_buffer);

//        LocalHeap::heapFree(nullptr, transfer.payload);
//        transfer.payload = nullptr; // Avoid dangling pointer
//        return true; // Indicate successful processing


        using ManagedT = ManagedCyphalTransfer<Heap>;
        SafeAllocator<ManagedT, Heap> alloc;
        auto transfer_ptr = std::allocate_shared<ManagedT>(alloc, transfer);
        service_manager->handleMessage(transfer_ptr);    	

        bool all_successful = true;
        std::apply([&](auto &...adapter)
                   { ([&]()
                      {
            int32_t res = adapter.cyphalTxForward(static_cast<CyphalMicrosecond>(0), &transfer.metadata, transfer.payload_size, transfer.payload, CYPHAL_NODE_ID_UNSET);
            all_successful = all_successful && (res > 0); }(), ...); }, adapters);
//        log(LOG_LEVEL_DEBUG, "LoopManager::processTransfer: shared counter %4d\r\n", transfer_ptr.use_count());
        return all_successful; // Return success status
    }

#if defined(HAL_CAN_MODULE_ENABLED) || defined(MOCK_HAL_CAN_ENABLED)
    template <size_t N, typename... Adapters>
    void CanProcessRxQueue(Cyphal<CanardAdapter> *cyphal, ServiceManager *service_manager, std::tuple<Adapters...> &adapters, CircularBuffer<CanRxFrame, N> &can_rx_buffer)
    {
        size_t num_frames = can_rx_buffer.size();
        for (uint32_t n = 0; n < num_frames; ++n)
        {
            CanRxFrame frame = can_rx_buffer.pop();
            size_t frame_size = frame.header.DLC;

//        	constexpr size_t BUFFER_SIZE = 256;
//        	char hex_string_buffer[BUFFER_SIZE];
//        	uchar_buffer_to_hex(frame.data, frame_size, hex_string_buffer, BUFFER_SIZE);
//            log(LOG_LEVEL_DEBUG, "LoopManager::CanProcessRxQueue dump: %4x %s\r\n", frame.header.ExtId, hex_string_buffer);

            CyphalTransfer transfer;
            int32_t result = cyphal->cyphalRxReceive(frame.header.ExtId, &frame_size, frame.data, &transfer);
            if (result == 1)
            {
                processTransfer(transfer, service_manager, adapters);
            }
        }
    }
#endif // defined(HAL_CAN_MODULE_ENABLED) || defined(MOCK_HAL_CAN_ENABLED)

    template <size_t N, typename... Adapters>
    void ProcessRxQueue(Cyphal<SerardAdapter> *cyphal, ServiceManager *service_manager, std::tuple<Adapters...> &adapters, CircularBuffer<SerialFrame, N> &buffer)
    {
        size_t num_frames = buffer.size();
        log(LOG_LEVEL_TRACE, "LoopManager::SerialProcessRxQueue size: %d\r\n", num_frames);
        for (uint32_t n = 0; n < num_frames; ++n)
        {
            SerialFrame frame = buffer.pop();
            size_t frame_size = frame.size;
            size_t shift = 0;

//        	constexpr size_t BUFFER_SIZE = 256;
//        	char hex_string_buffer[BUFFER_SIZE];
//        	uchar_buffer_to_hex(frame.data + shift, frame_size, hex_string_buffer, BUFFER_SIZE);
//            log(LOG_LEVEL_DEBUG, "LoopManager::SerialProcessRxQueue dump: %s\r\n", hex_string_buffer);

            CyphalTransfer transfer;
            for (;;)
            {
                int32_t result = cyphal->cyphalRxReceive(&frame_size, frame.data + shift, &transfer);

                if (result == 1)
                {
                    processTransfer(transfer, service_manager, adapters);
                }

                if (frame_size == 0)
                    break;
                shift = frame.size - frame_size;
            }
        }
    }

    template <typename... Adapters>
    void LoopProcessRxQueue(Cyphal<LoopardAdapter> *cyphal, ServiceManager *service_manager, std::tuple<Adapters...> &adapters)
    {
        CyphalTransfer transfer;
        while (cyphal->cyphalRxReceive(nullptr, nullptr, &transfer))
        {
            processTransfer(transfer, service_manager, adapters);
        }
    }

#if !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)
	void CanProcessTxQueue(CanardAdapter */*adapter*/, CAN_HandleTypeDef */*hcan*/)
	{
		tx_drainer.irq_safe_drain();
	}
#endif // !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)
};

#endif // RX_PROCESSING_HPP
