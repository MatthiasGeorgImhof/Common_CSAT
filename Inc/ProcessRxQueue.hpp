#ifndef RX_PROCESSING_HPP
#define RX_PROCESSING_HPP

#include <tuple>
#include <memory>

#include "cyphal.hpp"
#include "canard_adapter.hpp"
#include "loopard_adapter.hpp"

#include "CircularBuffer.hpp"
#include "Serialization.hpp"
#include "ServiceManager.hpp"
#include "o1heap.h"
#include "Logger.hpp"
#include "TxQueueDrainer.hpp"

using LocalHeap = HeapAllocation<65536>;

#include <cstdio>
#include <cstdint>
#include <cstddef>

/**
 * Formats a CAN frame into a string buffer.
 * Format: "ID: 0x18FF1234, Size: 8, Data: 01 02 03 04 05 06 07 08"
 */
int format_frame(char* buffer, size_t buffer_size, uint32_t ext_id, size_t size, const uint8_t* data) {
    if (!buffer || buffer_size == 0) return 0;

    // 1. Write the Extended ID and Size
    int written = std::snprintf(buffer, buffer_size, "ID: 0x%08ux, Size: %4lu, Data: ", ext_id, size);
    if (written < 0 || static_cast<size_t>(written) >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
        return written;
    }

    // 2. Append payload data in HEX
    for (size_t i = 0; i < size; ++i) {
        int ret = std::snprintf(buffer + static_cast<size_t>(written), buffer_size - static_cast<size_t>(written), "%02x ", data[i]);
        if (ret < 0 || static_cast<size_t>(ret) >= (buffer_size - static_cast<size_t>(written))) {
            // Buffer is full, stop safely
            break;
        }
        written += ret;
    }

    return written;
}

int format_frame(char* buffer, size_t buffer_size, const uint8_t* data, size_t size) {
    if (!buffer || buffer_size == 0) return 0;

    int written = 0;
    for (size_t i = 0; i < size; ++i) {
        int ret = std::snprintf(buffer + static_cast<size_t>(written), buffer_size - static_cast<size_t>(written), "%02x ", data[i]);
        if (ret < 0 || static_cast<size_t>(ret) >= (buffer_size - static_cast<size_t>(written))) {
            // Buffer is full, stop safely
            break;
        }
        written += ret;
    }

    return written;
}

template <typename Heap>
class LoopManager
{
public:
    LoopManager() = default;

    // Common transfer processing function
    template <typename... Adapters>
    bool processTransfer(CyphalTransfer &transfer, ServiceManager *service_manager, std::tuple<Adapters...> &adapters)
    {


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

    template <typename Buffer, typename... Adapters>
    void ProcessRxQueue(Cyphal<CanardAdapter> *cyphal, ServiceManager *service_manager, std::tuple<Adapters...> &adapters, Buffer &rx_buffer)
//    void ProcessRxQueue(Cyphal<CanardAdapter> *, ServiceManager *, std::tuple<Adapters...> &, Buffer &rx_buffer)
    {
        size_t num_frames = rx_buffer.size();
        for (uint32_t n = 0; n < num_frames; ++n)
        {
        	auto raw_frame = rx_buffer.pop();
            
        	auto frame = GetHeaderFrame(raw_frame);
            uint32_t ext_id = frame.extended_can_id;
            size_t size = static_cast<uint16_t>(frame.size);
            uint8_t* data = frame.payload;

			constexpr size_t BUFFER_SIZE = 512;
			char line[BUFFER_SIZE];
			format_frame(line, sizeof(line), ext_id, size, data);
//			format_frame(line, sizeof(line), reinterpret_cast<const uint8_t*>(&raw_frame), sizeof(raw_frame));
			log(LOG_LEVEL_DEBUG, "LoopManager::ProcessRxQueue: %s\r\n", line);

            CyphalTransfer transfer;
            int32_t result = cyphal->cyphalRxReceive(ext_id, &size, data, &transfer);
            if (result == 1)
            {
                processTransfer(transfer, service_manager, adapters);
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
};

#endif // RX_PROCESSING_HPP
