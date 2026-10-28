#pragma once

#ifdef __arm__
#include "stm32xxxx.h"
#else
#include "mock_hal.h"
#endif

#include "canard.h"
#include "cyphal.hpp"
#include "canard_adapter.hpp"

#include "Serialization.hpp"
#include "Socket.hpp"

//
// drain to rx buffer
//

template <typename Buffer>
class BufTxQueueDrainer
{
public:
    BufTxQueueDrainer(CanardAdapter* adapter, Buffer& buffer);

    void drain();

private:
    CanardAdapter* adapter_;
    Buffer& buffer_;
};

template<typename Buffer>
BufTxQueueDrainer<Buffer>::BufTxQueueDrainer(CanardAdapter* adapter, Buffer& buffer) : adapter_(adapter), buffer_(buffer)
{}

template<typename Buffer>
void BufTxQueueDrainer<Buffer>::drain()
{
	CanardTxQueueItem* ti = nullptr;
    while ((ti = canardTxPeek(&adapter_->que)) != nullptr)
    {
        canardTxPop(&adapter_->que, ti);
        auto& frame_ = buffer_.begin_write();
        frame_.extended_can_id = ti->frame.extended_can_id;
        frame_.size = static_cast<uint16_t>(ti->frame.payload.size);
        memcpy(frame_.payload, ti->frame.payload.data, frame_.size);
        buffer_.commit_write();

        canardTxFree(&adapter_->que, &adapter_->ins, ti);
    }
}

//
// drain to CAN
//

#if !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)

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


CanTxQueueDrainer::CanTxQueueDrainer(CanardAdapter* adapter, CAN_HandleTypeDef* hcan)
    : adapter_(adapter), hcan_(hcan)
{}

void CanTxQueueDrainer::drain()
{
	CanardTxQueueItem* ti = nullptr;
    while ((ti = canardTxPeek(&adapter_->que)) != nullptr)
    {
    	auto num_mailboxes = HAL_CAN_GetTxMailboxesFreeLevel(hcan_);
    	if (num_mailboxes == 0)
            break;

        canardTxPop(&adapter_->que, ti);
        CAN_TxHeaderTypeDef header;
        header.ExtId = ti->frame.extended_can_id;
        header.DLC   = static_cast<uint8_t>(ti->frame.payload.size);
        header.RTR   = CAN_RTR_DATA;
        header.IDE   = CAN_ID_EXT;

        uint32_t mailbox;
        HAL_CAN_AddTxMessage(hcan_, &header, (uint8_t*)ti->frame.payload.data, &mailbox);

        CyphalHeader cyphal_header = parse_header(header.ExtId);
        uint8_t transfer_id = reinterpret_cast<const uint8_t*>(ti->frame.payload.data)[header.DLC-1];
        log(LOG_LEVEL_INFO, "CanTxQueueDrainer mailbox %d of %d available: %3d -> %3d subject %3d transfer_id %2x\r\n",
        			mailbox, num_mailboxes, cyphal_header.source_id, cyphal_header.destination_id, cyphal_header.port_id, transfer_id);

        canardTxFree(&adapter_->que, &adapter_->ins, ti);
    }
}

void CanTxQueueDrainer::irq_safe_drain()
{
	drain();
}

#endif // !defined(__arm__) || defined(HAL_CAN_MODULE_ENABLED)

//
// drain to Serial
//

#if !defined(__arm__) || defined(HAL_UART_MODULE_ENABLED)

template<uint8_t MTU>
class SerialTxQueueDrainer
{
public:
    SerialTxQueueDrainer(CanardAdapter* adapter, UART_HandleTypeDef* huart);

    void drain();

private:
    CanardAdapter* adapter_;
    UART_HandleTypeDef* huart_;
};


template<uint8_t MTU>
SerialTxQueueDrainer<MTU>::SerialTxQueueDrainer(CanardAdapter* adapter, UART_HandleTypeDef* huart)
    : adapter_(adapter), huart_(huart)
{}

template<uint8_t MTU>
void SerialTxQueueDrainer<MTU>::drain()
{
	CanardTxQueueItem* ti = nullptr;
    while ((ti = canardTxPeek(&adapter_->que)) != nullptr)
    {
        if (HAL_UART_GetState(huart_) == HAL_UART_STATE_BUSY_TX)
            break;
        
        canardTxPop(&adapter_->que, ti);
        COBSFramer<MTU> framer;
        auto data_frame = framer.serialize(ti->frame);
        HAL_UART_Transmit(huart_, reinterpret_cast<uint8_t*>(&data_frame), sizeof(data_frame), 1000);

        canardTxFree(&adapter_->que, &adapter_->ins, ti);
    }
}

#endif // !defined(__arm__) || defined(HAL_UART_MODULE_ENABLED)

//
// drain to UDP socket
//

#if !defined(__arm__) || defined(HAL_SPI_MODULE_ENABLED)

template<typename UdpSocket, Endpoint destination, uint8_t MTU>
class UdpTxQueueDrainer
{
public:
    UdpTxQueueDrainer(CanardAdapter* adapter, UdpSocket* socket);

    void drain();

private:
    CanardAdapter* adapter_;
    UdpSocket* socket_;
};

template<typename UdpSocket, Endpoint destination, uint8_t MTU>
UdpTxQueueDrainer<UdpSocket, destination, MTU>::UdpTxQueueDrainer(CanardAdapter* adapter, UdpSocket* socket)
    : adapter_(adapter), socket_(socket)
{}

template<typename UdpSocket, Endpoint destination, uint8_t MTU>
void UdpTxQueueDrainer<UdpSocket, destination, MTU>::drain()
{
	CanardTxQueueItem* ti = nullptr;
    while ((ti = canardTxPeek(&adapter_->que)) != nullptr)
    {
        canardTxPop(&adapter_->que, ti);
        PlainFramer<MTU> framer;
        auto data_frame = framer.serialize(ti->frame);
        socket_->send(reinterpret_cast<uint8_t*>(&data_frame), sizeof(data_frame), destination);

        canardTxFree(&adapter_->que, &adapter_->ins, ti);
    }
}

#endif // !defined(__arm__) || defined(HAL_SPI_MODULE_ENABLED)
