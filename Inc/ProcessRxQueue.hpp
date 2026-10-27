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
    {
        size_t num_frames = rx_buffer.size();
        for (uint32_t n = 0; n < num_frames; ++n)
        {
            auto raw_frame = rx_buffer.pop();
            auto frame = GetHeaderFrame(raw_frame);
            
            uint32_t ext_id = frame.extended_can_id;
            size_t size = static_cast<uint16_t>(frame.size);
            uint8_t* data = frame.payload;
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
