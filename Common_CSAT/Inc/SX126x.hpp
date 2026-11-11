#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "Transport.hpp"

template < typename Transport, typename BusyPin, typename ResetPin, typename Dio1Pin> 
requires FullDuplexStreamTransport<Transport>
class SX126x
{
public:

    // ------------------------------------------------------------------------
    // Commands
    // ------------------------------------------------------------------------

    enum class Command : uint8_t
    {
        SetSleep              = 0x84,
        SetStandby            = 0x80,
        SetFs                 = 0xC1,

        SetTx                 = 0x83,
        SetRx                 = 0x82,

        SetPacketType         = 0x8A,

        SetRfFrequency        = 0x86,

        SetTxParams           = 0x8E,

        SetModulationParams   = 0x8B,
        SetPacketParams       = 0x8C,

        SetBufferBaseAddress  = 0x8F,

        WriteBuffer           = 0x0E,
        ReadBuffer            = 0x1E,

        GetStatus             = 0xC0,

        GetIrqStatus          = 0x12,
        ClearIrqStatus        = 0x02,

        GetRxBufferStatus     = 0x13,
        GetPacketStatus       = 0x14,

        SetDioIrqParams       = 0x08
    };

    // ------------------------------------------------------------------------
    // Packet Type
    // ------------------------------------------------------------------------

    enum class PacketType : uint8_t
    {
        GFSK = 0x00,
        LoRa = 0x01
    };

    // ------------------------------------------------------------------------
    // IRQ Flags
    // ------------------------------------------------------------------------

    enum class IRQ : uint16_t
    {
        TxDone            = 0x0001,
        RxDone            = 0x0002,

        PreambleDetected  = 0x0004,
        SyncWordValid     = 0x0008,

        HeaderValid       = 0x0010,
        HeaderErr         = 0x0020,

        CrcErr            = 0x0040,

        CadDone           = 0x0080,
        CadDetected       = 0x0100,

        Timeout           = 0x0200
    };

    struct PacketStatus
    {
        int8_t rssi = 0;
        int8_t snr  = 0;
    };

    // ------------------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------------------

    SX126x( Transport& transport, BusyPin busy, ResetPin reset, Dio1Pin dio1) : 
        transport_(transport), busy_(busy), reset_(reset), dio1_(dio1) {}

    // ------------------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------------------

    bool Init()
    {
        if (!Reset())
            return false;

        if (!SetStandby())
            return false;

        return true;
    }

    bool Reset()
    {
        reset_.low();
        HAL_Delay(10);

        reset_.high();
        HAL_Delay(20);

        wait_busy();

        return true;
    }

    // ------------------------------------------------------------------------
    // Basic Radio Control
    // ------------------------------------------------------------------------

    bool SetStandby()
    {
        uint8_t mode = 0x00;

        return WriteCommand( Command::SetStandby, &mode, 1);
    }

    bool SetPacketType(PacketType type)
    {
        uint8_t pkt = static_cast<uint8_t>(type);
        
        return WriteCommand( Command::SetPacketType, &pkt, 1); 
    }

    bool SetFrequency(uint32_t frequency_hz)
    {
        constexpr uint64_t XTAL = 32000000ULL;

        uint32_t frf = static_cast<uint32_t>( ((uint64_t)frequency_hz << 25) / XTAL);
        uint8_t cmd[4]
        {
            static_cast<uint8_t>(frf >> 24),
            static_cast<uint8_t>(frf >> 16),
            static_cast<uint8_t>(frf >> 8),
            static_cast<uint8_t>(frf)
        };

        return WriteCommand( Command::SetRfFrequency, cmd, sizeof(cmd));
    }

    bool SetTxPower(int8_t power_dbm)
    {
        uint8_t cmd[2]
        {
            static_cast<uint8_t>(power_dbm),
            0x04
        };

        return WriteCommand( Command::SetTxParams, cmd, sizeof(cmd));
    }

    // ------------------------------------------------------------------------
    // TX / RX
    // ------------------------------------------------------------------------

    bool StartRx()
    {
        uint8_t timeout[3]
        {
            0xFF,
            0xFF,
            0xFF
        };

        return WriteCommand( Command::SetRx, timeout, sizeof(timeout)); 
    }

    bool StartTx()
    {
        uint8_t timeout[3]
        {
            0x00,
            0x00,
            0x00
        };

        return WriteCommand( Command::SetTx, timeout, sizeof(timeout)); 
    }

    // ------------------------------------------------------------------------
    // FIFO
    // ------------------------------------------------------------------------

    bool WriteBuffer(
        uint8_t offset,
        const uint8_t* data,
        uint8_t len)
    {
        wait_busy();

        uint8_t tx[258];

        tx[0] = static_cast<uint8_t>(Command::WriteBuffer);

        tx[1] = offset;

        std::memcpy( &tx[2], data, len);
        
        return transport_.write( tx, len + 2); 
    }

    bool ReadBuffer(
        uint8_t offset,
        uint8_t* data,
        uint8_t len)
    {
        wait_busy();

        uint8_t tx[258] {};
        uint8_t rx[258] {};

        tx[0] = static_cast<uint8_t>(Command::ReadBuffer);

        tx[1] = offset;

        bool ok = transport_.transfer( tx, rx, len + 3);

        if (!ok) return false;

        std::memcpy( data, &rx[3], len);

        return true;
    }

    // ------------------------------------------------------------------------
    // IRQ
    // ------------------------------------------------------------------------

    uint16_t GetIrqStatus()
    {
        uint8_t irq[2] {};

        ReadCommand( Command::GetIrqStatus, irq, sizeof(irq));

        return static_cast<uint16_t>( (irq[0] << 8) | irq[1]);
    }

    void ClearIrqStatus(uint16_t flags)
    {
        uint8_t cmd[2]
        {
            static_cast<uint8_t>(flags >> 8),
            static_cast<uint8_t>(flags)
        };

        WriteCommand( Command::ClearIrqStatus, cmd, sizeof(cmd));
    }

    bool IsIRQAsserted() const
    {
        return dio1_.read();
    }

private:

    Transport& transport_;

    BusyPin busy_;
    ResetPin reset_;
    Dio1Pin dio1_;

    void wait_busy()
    {
        while (busy_.read())
        {
        }
    }

    bool WriteCommand( Command command, const uint8_t* data, uint16_t len)
    {
        wait_busy();

        uint8_t tx[258];

        tx[0] = static_cast<uint8_t>(command);

        if (len)
        {
            std::memcpy( &tx[1], data, len);
        }

        return transport_.write( tx, len + 1);
    }

    bool ReadCommand( Command command, uint8_t* data, uint16_t len)
    {
        wait_busy();

        uint8_t tx[258] {};
        uint8_t rx[258] {};

        tx[0] = static_cast<uint8_t>( command);

        bool ok = transport_.transfer( tx, rx, len + 2);

        if (!ok)
        {
            return false;
        }

        std::memcpy( data, &rx[2], len);

        return true;
    }
};