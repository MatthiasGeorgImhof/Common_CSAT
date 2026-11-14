#pragma once 

#include <cstdint>
#include "SX126x.hpp"
#include "SX126xConfigurator.hpp"

template<typename Radio, typename Config, typename RxBuffer>
class SX126xController
{
public:
    enum class SX126xState
    {
        HALT,
        RX,
        TX_ACTIVE,
    };
    
    SX126xController( Radio& radio, Config& config, RxBuffer& rx_buffer) : radio_(radio), config_(config), rx_buffer_(rx_buffer) {}

    void process();

    void notify_irq();

    bool transmit( const uint8_t* data, uint8_t len);

    bool busy() const;

    void start();

    void stop();

    SX126xState state() const;

private:

    void enter_rx();

    void enter_tx( const uint8_t* data, uint8_t len);

    void handle_irq();

    void handle_rx_done();

    void handle_tx_done();

    void handle_crc_error();

    void handle_timeout();

    bool isTxDone(sx126x::IRQ irq)  { return static_cast<bool>(irq & sx126x::IRQ::TxDone); };
    bool isRxDone(sx126x::IRQ irq)  { return static_cast<bool>(irq & sx126x::IRQ::RxDone); };
    bool isCrcErr(sx126x::IRQ irq)  { return static_cast<bool>(irq & sx126x::IRQ::CrcErr); };
    bool isTimeout(sx126x::IRQ irq)  { return static_cast<bool>(irq & sx126x::IRQ::Timeout); };

private:

    Radio& radio_;
    Config& config_;
    RxBuffer& rx_buffer_;

    volatile bool irq_pending_ = false;

    SX126xState state_ = SX126xState::HALT;
};

template< typename Radio, typename Config, typename RxBuffer>
bool SX126xController<Radio, Config, RxBuffer>::transmit( const uint8_t* data, uint8_t len)
{
    if(state_ != SX126xState::RX)
        return false;

    auto packet = config_.packet;
    packet.payload_length = len;
    radio_.SetPacketParams(packet);
    radio_.WriteBuffer( 0, data, len);
    radio_.StartTx();

    state_ = SX126xState::TX_ACTIVE;

    return true;
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::handle_tx_done()
{
    enter_rx();
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::enter_rx()
{
    auto packet = config_.packet;
    packet.payload_length = sx126x::MTU;
    radio_.SetPacketParams(packet);
    radio_.StartRx();

    state_ = SX126xState::RX;
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::handle_rx_done()
{
    auto rx_info = radio_.GetRxBufferStatus();
    
    auto& frame = rx_buffer_.begin_write();
    radio_.ReadBuffer( rx_info.buffer_pointer, reinterpret_cast<uint8_t*>(&frame), rx_info.payload_length);
    rx_buffer_.commit_write();
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::handle_crc_error()
{
    enter_rx();
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::handle_timeout()
{
    enter_rx();
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::process()
{
    if (!irq_pending_)
        return;

    irq_pending_ = false;

    auto irq = radio_.GetIrqStatus();
    radio_.ClearIrqStatus(irq);

    if (isTxDone(irq))
        handle_tx_done();

    if (isRxDone(irq))
        handle_rx_done();

    if (isCrcErr(irq))
        handle_crc_error();

    if (isTimeout(irq))
        handle_timeout();
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::notify_irq()
{
    irq_pending_ = true;
}

template< typename Radio, typename Config, typename RxBuffer>
bool SX126xController<Radio, Config, RxBuffer>::busy() const
{
    return state_ != SX126xState::RX;
}

template< typename Radio, typename Config, typename RxBuffer>
SX126xController<Radio, Config, RxBuffer>::SX126xState SX126xController<Radio, Config, RxBuffer>::state() const
{
    return state_;
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::start()
{
    enter_rx();
}

template< typename Radio, typename Config, typename RxBuffer>
void SX126xController<Radio, Config, RxBuffer>::stop()
{
    radio_.SetStandby();
    state_ = SX126xState::HALT;
}

