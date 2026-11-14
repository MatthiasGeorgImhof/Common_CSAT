#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "Transport.hpp"
#include "BitmaskEnum.hpp"

namespace sx126x
{
    enum class Command : uint8_t
    {
        ResetStats         	  = 0x00,
        ClearIrqStatus        = 0x02,
		ClearDeviceErrors     = 0x07,
        SetDioIrqParams       = 0x08,
        WriteRegister         = 0x0D,
        WriteBuffer           = 0x0E,

        GetStats         	  = 0x10,
        GetPacketType         = 0x11,
		GetIrqStatus          = 0x12,
        GetRxBufferStatus     = 0x13,
        GetPacketStatus       = 0x14,
        GetRssiInst    		  = 0x15,
        GetDeviceErrors       = 0x17,
        ReadTegister          = 0x1D,
        ReadBuffer            = 0x1E,

		SetStandby            = 0x80,
        SetRx                 = 0x82,
        SetTx                 = 0x83,
        SetSleep              = 0x84,
        SetRfFrequency        = 0x86,
        SetCadParams          = 0x88,
        Calibrate             = 0x89,
        SetPacketType         = 0x8A,
        SetModulationParams   = 0x8B,
        SetPacketParams       = 0x8C,
        SetTxParams           = 0x8E,
        SetBufferBaseAddress  = 0x8F,

		SetRxTxFallBackMode   = 0x93,
		SetRxDutyCycle        = 0x94,
		SetPaConfig           = 0x95,
		SetRegulatoMode       = 0x96,
		SetDio3AsTcxoCtrl     = 0x97,
		SetCalibrateImage     = 0x98,
		SetDio2AsRfSwitchCtrl = 0x9D,
		StopTimerOnPreamble	  = 0x9F,

		SetLoRaSymbNumTimeout = 0xA0,

        GetStatus             = 0xC0,
        SetFs                 = 0xC1,
        SetCad                = 0xC5,

		SetTxContinuousWave   = 0xD1,
		SetTxInfinitePreamble = 0xD2,
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
    // Header Type
    // ------------------------------------------------------------------------
    
    enum class HeaderType : uint8_t
    {
        Variable = 0x00,
        Fixed = 0x01
    };

    // ------------------------------------------------------------------------
    // IRQ Flags
    // ------------------------------------------------------------------------

    enum class IRQ : uint16_t
    {
        None			  = 0x0000,

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
    ENABLE_BITMASK_OPERATORS(IRQ)

    struct PacketStatus
    {
        int16_t rssi_dbm;
        int8_t  snr_db;
        int16_t signal_rssi_dbm;
    };

    struct PaConfig
	{
    	uint8_t paDutyCycle;
    	uint8_t hpMax;
    	uint8_t deviceSel;
    	uint8_t paLut = 0x01;
	};

    struct LoRaModulationParams
    {
        uint8_t sf;
        uint8_t bw;
        uint8_t cr;
        uint8_t ldro;
    };

   struct LoRaPacketParams
    {
        uint16_t preamble_length;

        HeaderType header_type;
        uint8_t payload_length;

        uint8_t crc;
        uint8_t iq;
    };

   static constexpr uint8_t MTU = 255;

    struct IrqMasks {
    	sx126x::IRQ IrqMask;
    	sx126x::IRQ Dio1Mask;
    	sx126x::IRQ Dio2Mask;
    	sx126x::IRQ Dio3Mask;
    };

    struct RxBufferStatus
    {
        uint8_t payload_length;
        uint8_t buffer_pointer;
    };

} // namespace sx126x

template < typename Transport, typename BusyPin, typename ResetPin, typename Dio1Pin> 
requires FullDuplexStreamTransport<Transport>
class SX126x
{
public:

    // ------------------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------------------

    SX126x( Transport& transport) : transport_(transport) {}

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

    uint16_t GetDeviceErrors()
    {
        uint16_t err{0};
        ReadCommand(sx126x::Command::GetDeviceErrors, reinterpret_cast<uint8_t*>(&err), sizeof(err));
        return swap16(err);
    }

    void ClearDeviceErrors()
    {
        uint8_t buf[2] = {0x00, 0x00};
        WriteCommand(sx126x::Command::ClearDeviceErrors, buf, sizeof(buf));
    }

    // ------------------------------------------------------------------------
    // Status & Info
    // ------------------------------------------------------------------------

    uint8_t GetStatus()
    {
        uint8_t status = 0;

        ReadCommand(sx126x::Command::GetStatus, &status, 1);

        return status;
    }

    sx126x::RxBufferStatus GetRxBufferStatus()
    {
        uint8_t buf[2] = {0};
        ReadCommand(sx126x::Command::GetRxBufferStatus, buf, sizeof(buf));
        return { buf[0], buf[1] }; // [0] = PayloadLengthRx, [1] = RxStartBufferPointer
    }
    
    sx126x::PacketStatus GetPacketStatus()
    {
        uint8_t buf[3] = {0};
        ReadCommand(sx126x::Command::GetPacketStatus, buf, sizeof(buf));

        return {
            static_cast<int16_t>(-static_cast<int16_t>(buf[0]) / 2),
            static_cast<int8_t>(static_cast<int8_t>(buf[1]) / 4),
            static_cast<int16_t>(-static_cast<int16_t>(buf[2]) / 2)
        };
    }

    int16_t GetRssiInst()
    {
        uint8_t rssi = 0;
        ReadCommand(sx126x::Command::GetRssiInst, &rssi, sizeof(rssi));
        return static_cast<int16_t>(-static_cast<int16_t>(rssi) / 2);
    }

    // ------------------------------------------------------------------------
    // Basic Radio Control
    // ------------------------------------------------------------------------

    bool SetStandby()
    {
        uint8_t mode = 0x00;

        return WriteCommand(sx126x::Command::SetStandby, &mode, 1);
    }

    bool SetPacketType(sx126x::PacketType type)
    {
        uint8_t pkt = static_cast<uint8_t>(type);
        
        return WriteCommand(sx126x::Command::SetPacketType, &pkt, 1); 
    }

    bool SetFrequency(uint32_t frequency_hz)
    {
        constexpr uint64_t XTAL = 32000000ULL;

        uint32_t frf = static_cast<uint32_t>( ((uint64_t)frequency_hz << 25) / XTAL);
        uint32_t frf_swapped = swap32(frf);

        return WriteCommand(sx126x::Command::SetRfFrequency, reinterpret_cast<const uint8_t*>(&frf_swapped), sizeof(frf_swapped));
    }

    bool SetTxPower(int8_t power_dbm)
    {
        uint8_t cmd[2]
        {
            static_cast<uint8_t>(power_dbm),
            0x04
        };

        return WriteCommand(sx126x::Command::SetTxParams, cmd, sizeof(cmd));
    }

    // ------------------------------------------------------------------------
    // Advanced Radio Control
    // ------------------------------------------------------------------------

    bool SetBufferBaseAddress(uint8_t tx_base, uint8_t rx_base)
    {
        uint8_t addresses[2]
        {
            tx_base,
			rx_base
        };

        return WriteCommand(sx126x::Command::SetBufferBaseAddress, addresses, sizeof(addresses));
    }

    bool SetModulationParams(const sx126x::LoRaModulationParams params)
    {
        return WriteCommand(sx126x::Command::SetModulationParams, reinterpret_cast<const uint8_t*>(&params), sizeof(params));
    }

    bool SetPacketParams(const sx126x::LoRaPacketParams params)
    {
        const sx126x::LoRaPacketParams params_ {
        		swap16(params.preamble_length), params.header_type, params.payload_length, params.crc, params.iq
        };

    	return WriteCommand(sx126x::Command::SetPacketParams, reinterpret_cast<const uint8_t*>(&params_), sizeof(params_));
    }

    // For SX1262 modules (+22 dBm):
    // paDutyCycle = 0x04, hpMax = 0x07, deviceSel = 0x00, paLut = 0x01
    // For SX1261 modules (+14 dBm):
    // paDutyCycle = 0x04, hpMax = 0x00, deviceSel = 0x01, paLut = 0x01
    bool SetPaConfig(const sx126x::PaConfig params)
    {
        return WriteCommand(sx126x::Command::SetPaConfig, reinterpret_cast<const uint8_t*>(&params), sizeof(params));
    }

    bool SetDio2AsRfSwitchCtrl(bool enable = true)
    {
        uint8_t val = enable ? 0x01 : 0x00;
        return WriteCommand(sx126x::Command::SetDio2AsRfSwitchCtrl, &val, 1);
    }

    // voltage: 0x00=1.6V, 0x01=1.7V, 0x02=1.8V, 0x03=2.2V, 0x04=2.4V, 0x05=2.7V, 0x06=3.0V, 0x07=3.3V
    // timeout: ticks in 15.625 us steps (~320 = 5 ms)
    bool SetDio3AsTcxoCtrl(uint8_t tcxoVoltage, uint32_t timeoutTicks)
    {
        uint8_t buf[4] = {
            tcxoVoltage,
            static_cast<uint8_t>(timeoutTicks >> 16),
            static_cast<uint8_t>(timeoutTicks >> 8),
            static_cast<uint8_t>(timeoutTicks)
        };
        return WriteCommand(sx126x::Command::SetDio3AsTcxoCtrl, buf, sizeof(buf));
    }


    // ------------------------------------------------------------------------
    // TX / RX
    // ------------------------------------------------------------------------

    bool StartRx()
    {
        uint8_t timeout[3] { 0xFF, 0xFF, 0xFF };
        return WriteCommand(sx126x::Command::SetRx, timeout, sizeof(timeout)); 
    }

    bool StartTx()
    {
        uint8_t timeout[3] { 0x00, 0x00, 0x00 };
        return WriteCommand(sx126x::Command::SetTx, timeout, sizeof(timeout)); 
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

        tx[0] = static_cast<uint8_t>(sx126x::Command::WriteBuffer);

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

        tx[0] = static_cast<uint8_t>(sx126x::Command::ReadBuffer);

        tx[1] = offset;

        bool ok = transport_.transfer( tx, rx, len + 3);

        if (!ok) return false;

        std::memcpy( data, &rx[3], len);

        return true;
    }

    // ------------------------------------------------------------------------
    // IRQ
    // ------------------------------------------------------------------------

    bool SetDioIrqParams(const sx126x::IrqMasks masks)
    {
        uint16_t masks_[] = {
            swap16(static_cast<uint16_t>(masks.IrqMask)),
            swap16(static_cast<uint16_t>(masks.Dio1Mask)),
            swap16(static_cast<uint16_t>(masks.Dio2Mask)),
            swap16(static_cast<uint16_t>(masks.Dio3Mask))
        };
        return WriteCommand(sx126x::Command::SetDioIrqParams, reinterpret_cast<const uint8_t*>(&masks_), sizeof(masks_));
    }

    sx126x::IRQ GetIrqStatus()
    {
        uint16_t irq{};
        ReadCommand(sx126x::Command::GetIrqStatus, reinterpret_cast<uint8_t*>(&irq), sizeof(irq));
        return sx126x::IRQ(swap16(irq));
    }

    void ClearIrqStatus(sx126x::IRQ flags)
    {
        uint16_t flags_swapped = swap16(static_cast<uint16_t>(flags));
        WriteCommand(sx126x::Command::ClearIrqStatus, reinterpret_cast<const uint8_t*>(&flags_swapped), sizeof(flags_swapped));
    }

    bool IsIRQAsserted() const
    {
        return dio1_.read();
    }

private:

    void wait_busy()
    {
        while (busy_.read())
        {
        }
    }

    bool WriteCommand(sx126x::Command command, const uint8_t* data, uint16_t len)
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

    bool ReadCommand(sx126x::Command command, uint8_t* data, uint16_t len)
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

	constexpr uint16_t swap16(uint16_t val) {
		return (val << 8) | (val >> 8);
	}

	constexpr uint32_t swap32(uint32_t val) {
		return ((val << 24) & 0xFF000000) |
			   ((val <<  8) & 0x00FF0000) |
			   ((val >>  8) & 0x0000FF00) |
			   ((val >> 24) & 0x000000FF);
	}

private:
    Transport& transport_;

    BusyPin busy_{};
    ResetPin reset_{};
    Dio1Pin dio1_{};
};
