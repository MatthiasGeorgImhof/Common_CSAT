/*
 * CC1101Manager.hpp
 *
 *  Created on: Apr 16, 2026
 *      Author: mgi
 */

#pragma once

#include <array>
#include <cstdint>
#include <algorithm>

#include "CC1101.hpp"
#include "CircularBuffer.hpp"

#include "Logger.hpp"

// -----------------------------------------------------------------------------
// CC1101 Packet Layout
// [0]     = length (1..61)
// [1..N]  = payload
// [N+1]   = RSSI
// [N+2]   = LQI | CRC_OK bit
// -----------------------------------------------------------------------------

using CC1101Packet = std::array<std::uint8_t, 64>;

// -----------------------------------------------------------------------------
// Packet View (zero-copy, DMA-friendly)
// -----------------------------------------------------------------------------

struct CC1101PacketView
{
    uint8_t* data;

    explicit CC1101PacketView(uint8_t* ptr) : data(ptr) {}

    uint8_t  len() const { return data[0]; }
    void     set_len(uint8_t v) { data[0] = v; }

    uint8_t* payload() { return &data[1]; }
    const uint8_t* payload() const { return &data[1]; }

    uint8_t  rssi() const { return data[len() + 1]; }

    uint8_t  raw_lqi() const { return data[len() + 2]; }
    uint8_t  lqi() const { return raw_lqi() & 0x7F; }

    bool     crc_ok() const { return (raw_lqi() & 0x80) != 0; }
};

// -----------------------------------------------------------------------------
// Radio State Machine
// -----------------------------------------------------------------------------

enum class RadioState
{
    INIT,
    RX_LISTENING,
    RX_PROCESSING,
    TX_START,
    TX_WAIT_END
};

// -----------------------------------------------------------------------------
// CC1101 Manager (packet-level, DMA-ready)
// -----------------------------------------------------------------------------

template <typename RADIO, typename TXRing, typename RXRing, typename GD2Pin>
class CC1101Manager
{
public:
CC1101Manager(RADIO& radio, TXRing& tx_ring, RXRing& rx_ring, GD2Pin gd2) : radio_(radio), tx_ring_(tx_ring), rx_ring_(rx_ring), gd2_(gd2) {}

    // -------------------------------------------------------------------------
    // Public API: Packet-level TX/RX
    // -------------------------------------------------------------------------

    bool push_tx_packet(const CC1101Packet& pkt)
    {
        if (tx_ring_.is_full())
            return false;

        tx_ring_.push(pkt);
        return true;
    }

    bool pop_rx_packet(CC1101Packet& out)
    {
        if (rx_ring_.is_empty())
            return false;

        out = rx_ring_.pop();
        return true;
    }

    // Called from GDO0 interrupt
    void notify_packet_received()
    {
        packet_received_flag_ = true;
    }

    // Main state machine
    void process()
    {
        switch (state_)
        {
        case RadioState::INIT:
            radio_.Init();
            force_rx();
            break;

        case RadioState::RX_LISTENING:
            handle_rx_listening();
            break;

        case RadioState::RX_PROCESSING:
            handle_rf_read();
            force_rx();
            break;

        case RadioState::TX_START:
            handle_rf_tx();
            break;

        case RadioState::TX_WAIT_END:
            handle_tx_wait_end();
            break;
        }
    }

private:
    RADIO& radio_;
    RadioState state_ = RadioState::INIT;

    TXRing& tx_ring_;   // CC1101Packet
    RXRing& rx_ring_;   // CC1101Packet

    GD2Pin gd2_;

    CC1101Packet scratch_{};

    volatile bool packet_received_flag_ = false;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    void force_rx()
    {
        radio_.Strobe(RADIO::StrobeCommand::SIDLE);
        radio_.Strobe(RADIO::StrobeCommand::SFRX);
        radio_.Strobe(RADIO::StrobeCommand::SRX);
        state_ = RadioState::RX_LISTENING;
    }

    void handle_rx_listening()
    {
        // Check for RX overflow
        uint8_t marc = radio_.ReadStatus(RADIO::StatusRegister::MARCSTATE) & 0x1F;
        if (marc == 0x11) // RX_OVERFLOW
        {
            force_rx();
            return;
        }

        // Incoming packet?
        if (packet_received_flag_)
        {
            packet_received_flag_ = false;
            state_ = RadioState::RX_PROCESSING;
            return;
        }

        // Air clear + TX data available?
        if (!tx_ring_.is_empty() && gd2_.read() == false)
        {
            state_ = RadioState::TX_START;
        }
    }

    // -------------------------------------------------------------------------
    // RX path: DMA-ready, zero-copy
    // -------------------------------------------------------------------------

    void handle_rf_read()
    {
        uint8_t rxStatus = radio_.ReadStatus(RADIO::StatusRegister::RXBYTES);
        uint8_t bytesInFifo = rxStatus & 0x7F;

        if ((rxStatus & 0x80) || bytesInFifo < 3)
            return;

        // Reserve slot in RX ring
        CC1101Packet& slot = rx_ring_.begin_write();

        // Read raw CC1101 FIFO into slot
        radio_.ReadBurst(RADIO::BurstRegister::RXFIFO, slot.data(), bytesInFifo);

//         char buffer[1024];
//         uchar_buffer_to_hex(slot.data()+1, slot[0], buffer, sizeof(buffer));
//         log(LOG_LEVEL_DEBUG, "handle_rf_read frame at %08u: %s\r\n", HAL_GetTick(), buffer);

        CC1101PacketView pkt(slot.data());

        uint8_t len = pkt.len();
        if (len == 0 || len > 61)
            return;

        if (bytesInFifo < len + 3)
            return;

        if (!pkt.crc_ok())
            return;

        // Valid packet → commit
        rx_ring_.commit_write();
    }

    // -------------------------------------------------------------------------
    // TX path: packet-level
    // -------------------------------------------------------------------------

    void handle_rf_tx()
    {
        if (tx_ring_.is_empty())
        {
            state_ = RadioState::RX_LISTENING;
            return;
        }

        CC1101Packet pkt = tx_ring_.pop();
        CC1101PacketView view(pkt.data());

        uint8_t len = view.len();
        if (len == 0 || len > 61)
        {
            state_ = RadioState::RX_LISTENING;
            return;
        }

        radio_.Strobe(RADIO::StrobeCommand::SIDLE);
        radio_.Strobe(RADIO::StrobeCommand::SFTX);
        radio_.WriteBurst(RADIO::BurstRegister::TXFIFO, pkt.data(), len + 1);
        radio_.Strobe(RADIO::StrobeCommand::STX);

        state_ = RadioState::TX_WAIT_END;

        // char out[256];
        // uchar_buffer_to_hex(reinterpret_cast<const unsigned char*>(view.payload()), len, out, sizeof(out));
        // log(LOG_LEVEL_DEBUG, "handle_rf_tx %d: %s\r\n", len, out);
    }

    void handle_tx_wait_end()
    {
        uint8_t marc = radio_.ReadStatus(RADIO::StatusRegister::MARCSTATE) & 0x1F;

        if (marc == 0x01) // IDLE
        {
            force_rx();
        }
        else if (marc == 0x16) // TX_UNDERFLOW
        {
            radio_.Strobe(RADIO::StrobeCommand::SFTX);
            force_rx();
        }
    }
};
