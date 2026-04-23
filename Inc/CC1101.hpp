/**
 * @file CC1101.hpp
 * @brief Minimal CC1101 driver interface for TX and RX operation.
 *
 * This header exposes a compact API for:
 *   - Initializing the CC1101 transceiver
 *   - Issuing strobe commands (SRES, STX, SIDLE, SFTX, etc.)
 *   - Reading/writing single registers
 *   - Performing burst FIFO transfers
 *
 * The implementation configures the radio for:
 *   - 433.92 MHz center frequency
 *   - 38.4 kbps 2‑FSK
 *   - Whitening + CRC
 *   - Variable‑length packets (max payload = 61 bytes)
 *
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "Transport.hpp" // Include the transport layers and concepts

template <typename Transport, typename CSPin, typename MISOPin>
	requires RegisterAccessTransport<Transport>
class CC1101 {
public:
	// -------------------------------------------------------------------------
	// Nested Types and Enums
	// -------------------------------------------------------------------------

	static constexpr uint32_t F_CARRIER = 433920000; // 433.92 MHz
	static constexpr uint32_t F_OSC = 26000000;		 // 26 MHz crystal
													 // TI Formula: (F_CARRIER * 2^16) / F_OSC
	static constexpr uint32_t FREQ = static_cast<uint32_t>((static_cast<uint64_t>(F_CARRIER) << 16) / F_OSC);

	enum class ConfigurationRegister : uint8_t
	{
		IOCFG2 = 0x00,	 // GDO2 output pin configuration
		IOCFG1 = 0x01,	 // GDO1 output pin configuration
		IOCFG0 = 0x02,	 // GDO0 output pin configuration
		FIFOTHR = 0x03,	 // RX FIFO and TX FIFO thresholds
		SYNC1 = 0x04,	 // Sync word, high byte
		SYNC0 = 0x05,	 // Sync word, low byte
		PKTLEN = 0x06,	 // Packet length
		PKTCTRL1 = 0x07, // Packet automation control
		PKTCTRL0 = 0x08, // Packet automation control
		ADDR = 0x09,	 // Device address
		CHANNR = 0x0A,	 // Channel number
		FSCTRL1 = 0x0B,	 // Frequency synthesizer control
		FSCTRL0 = 0x0C,	 // Frequency synthesizer control
		FREQ2 = 0x0D,	 // Frequency control word, high byte
		FREQ1 = 0x0E,	 // Frequency control word, middle byte
		FREQ0 = 0x0F,	 // Frequency control word, low byte
		MDMCFG4 = 0x10,	 // Modem configuration
		MDMCFG3 = 0x11,	 // Modem configuration
		MDMCFG2 = 0x12,	 // Modem configuration
		MDMCFG1 = 0x13,	 // Modem configuration
		MDMCFG0 = 0x14,	 // Modem configuration
		DEVIATN = 0x15,	 // Modem deviation setting
		MCSM2 = 0x16,	 // Main Radio Control State Machine configuration
		MCSM1 = 0x17,	 // Main Radio Control State Machine configuration
		MCSM0 = 0x18,	 // Main Radio Control State Machine configuration
		FOCCFG = 0x19,	 // Frequency Offset Compensation configuration
		BSCFG = 0x1A,	 // Bit Synchronization configuration
		AGCTRL2 = 0x1B,	 // AGC control
		AGCTRL1 = 0x1C,	 // AGC control
		AGCTRL0 = 0x1D,	 // AGC control
		WOREVT1 = 0x1E,	 // High byte Event 0 timeout
		WOREVT0 = 0x1F,	 // Low byte Event 0 timeout
		WORCTRL = 0x20,	 // Wake On Radio control
		FREND1 = 0x21,	 // Front end RX configuration
		FREND0 = 0x22,	 // Front end TX configuration
		FSCAL3 = 0x23,	 // Frequency synthesizer calibration
		FSCAL2 = 0x24,	 // Frequency synthesizer calibration
		FSCAL1 = 0x25,	 // Frequency synthesizer calibration
		FSCAL0 = 0x26,	 // Frequency synthesizer calibration
		RCCTRL1 = 0x27,	 // RC oscillator configuration
		RCCTRL0 = 0x28,	 // RC oscillator configuration
		FSTEST = 0x29,	 // Frequency synthesizer calibration control
		PTEST = 0x2A,	 // Production test
		AGCTEST = 0x2B,	 // AGC test
		TEST2 = 0x2C,	 // Various test settings
		TEST1 = 0x2D,	 // Various test settings
		TEST0 = 0x2E,	 // Various test settings

		PATABLE = 0x3E, // TX Power
	};

	enum class StrobeCommand : uint8_t
	{
		SRES = 0x30,	// Reset chip
		SFSTXON = 0x31, // Enable and calibrate frequency synthesizer
		SXOFF = 0x32,	// Turn off crystal oscillator
		SCAL = 0x33,	// Calibrate frequency synthesizer and turn it off
		SRX = 0x34,		// Enable RX
		STX = 0x35,		// In IDLE state: Enable TX
		SIDLE = 0x36,	// Exit RX / TX
		SWOR = 0x38,	// Start automatic RX polling sequence
		SPWD = 0x39,	// Enter power down mode when CSn goes high
		SFRX = 0x3A,	// Flush the RX FIFO buffer
		SFTX = 0x3B,	// Flush the TX FIFO buffer
		SWORRST = 0x3C, // Reset real time clock to Event1 value
		SNOP = 0x3D,	// No operation
	};

	enum class StatusRegister : uint8_t
	{
		PARTNUM = 0x30,		   // Part number for CC1101
		VERSION = 0x31,		   // Current version number
		FREQEST = 0x32,		   // Frequency Offset Estimate
		LQI = 0x33,			   // Demodulator estimate for Link Quality
		RSSI = 0x34,		   // Received signal strength indication
		MARCSTATE = 0x35,	   // Control state machine state
		WORTIME1 = 0x36,	   // High byte of WOR timer
		WORTIME0 = 0x37,	   // Low byte of WOR timer
		PKTSTATUS = 0x38,	   // Current GDOx status and packet status
		VCO_VC_DAC = 0x39,	   // Current setting from PLL calibration module
		TXBYTES = 0x3A,		   // Underflow and number of bytes in the TX FIFO
		RXBYTES = 0x3B,		   // Overflow and number of bytes in the RX FIFO
		RCCTRL1_STATUS = 0x3C, // Last RC oscillator calibration result
		RCCTRL0_STATUS = 0x3D, // Last RC oscillator calibration result
	};

	enum class BurstRegister : uint8_t
	{
		PATABLE = 0x3E, // Power Amplifier table address
		TXFIFO = 0x3F,	// TX FIFO address
		RXFIFO = 0x3F,	// RX FIFO address (same as TXFIFO)
	};

	enum class TXPower : uint8_t
	{
		LOW = 0x03,	   // -30dBm
		BENCH = 0x2D,  //  -6dBm
		NORMAL = 0x50, //   0dBm
		HIGH = 0x0C,   // +10dBm
	};

	struct RegisterValueTuple
	{
		ConfigurationRegister addr;
		uint8_t value;
	};

	static constexpr uint8_t TXFIFO_UNDERFLOW = 0x80;
	static constexpr uint8_t RXFIFO_OVERFLOW = 0x80;
	static constexpr uint8_t CRC_OK = 0x80;

	static constexpr uint8_t READ = 0x80;
	static constexpr uint8_t WRITE = 0x00;
	static constexpr uint8_t BURST = 0x40;

	// -------------------------------------------------------------------------
	// Constructor
	// -------------------------------------------------------------------------

explicit CC1101(Transport& transport, CSPin cs, MISOPin miso)
    : transport_(transport), cs_(cs), miso_(miso) {}

	// -------------------------------------------------------------------------
	// Public Methods
	// -------------------------------------------------------------------------

	/**
	 * @brief Initialize the CC1101 transceiver.
	 */
	void Init()
	{
		Reset();

		Configure(init_regs, sizeof(init_regs) / sizeof(init_regs[0]));

		// Flush FIFOs
		Strobe(StrobeCommand::SFRX);
		Strobe(StrobeCommand::SFTX);

		// Enter SLEEP — RX state machine will explicitly issue SRX
		Strobe(StrobeCommand::SIDLE);
		Strobe(StrobeCommand::SPWD);
	}

	/**
	 * @brief Configure CC1101 with an array of register-value pairs.
	 */
	void Configure(const RegisterValueTuple *tuples, size_t num_tuples)
	{
		for (size_t i = 0; i < num_tuples; ++i)
		{
			WriteConfiguration(tuples[i].addr, tuples[i].value);
		}
	}

	/**
	 * @brief Send a CC1101 strobe command.
	 */
	void Strobe(StrobeCommand strobe)
	{
		// Writing a register address with len=0 triggers the command strobe on CC1101
		transport_.write_reg(static_cast<uint8_t>(strobe), nullptr, 0);
	}

	/**
	 * @brief Write a single CC1101 register.
	 */
	void WriteConfiguration(ConfigurationRegister addr, uint8_t value)
	{
		transport_.write_reg(static_cast<uint8_t>(addr) | WRITE, &value, 1);
	}

	/**
	 * @brief Read a single CC1101 register.
	 */
	uint8_t ReadConfiguration(ConfigurationRegister addr)
	{
		uint8_t value = 0;
		transport_.read_reg(static_cast<uint8_t>(addr) | READ, &value, 1);
		return value;
	}

	/**
	 * @brief Read a single CC1101 status register.
	 */
	uint8_t ReadStatus(StatusRegister addr)
	{
		// Status registers require both bits 0x80 (Read) AND 0x40 (Burst)
		uint8_t value = 0;
		transport_.read_reg(static_cast<uint8_t>(addr) | READ | BURST, &value, 1);
		return value;
	}

	/**
	 * @brief Write multiple bytes to a CC1101 FIFO or burst‑capable register.
	 */
	void WriteBurst(BurstRegister addr, const uint8_t *data, uint8_t len)
	{
		if (len > 64)
			len = 64;
		transport_.write_reg(static_cast<uint8_t>(addr) | BURST | WRITE, data, len);
	}

	/**
	 * @brief Read multiple bytes from a CC1101 FIFO or burst‑capable register.
	 */
	void ReadBurst(BurstRegister addr, uint8_t *data, uint8_t len)
	{
		transport_.read_reg(static_cast<uint8_t>(addr) | BURST | READ, data, len);
	}

	/**
	 * @brief Set TX power.
	 */
	void SetPower(TXPower dBm)
	{
		WriteConfiguration(ConfigurationRegister::PATABLE, static_cast<uint8_t>(dBm));
	}

	/**
	 * @brief Set carrier frequency.
	 */
	void setCarrierFrequency(uint32_t f_carrier)
	{
		uint8_t f2, f1, f0;
		cc1101_compute_freq(f_carrier, f2, f1, f0);

		WriteConfiguration(ConfigurationRegister::FREQ2, f2);
		WriteConfiguration(ConfigurationRegister::FREQ1, f1);
		WriteConfiguration(ConfigurationRegister::FREQ0, f0);
	}

	static inline void cc1101_compute_freq(uint32_t f_carrier, uint8_t &FREQ2, uint8_t &FREQ1, uint8_t &FREQ0)
	{
		constexpr uint32_t f_osc = 26000000UL; // 26 MHz crystal
		uint32_t freq_reg = (uint32_t)((((uint64_t)f_carrier) << 16) / f_osc);

		FREQ2 = (freq_reg >> 16) & 0xFF;
		FREQ1 = (freq_reg >> 8) & 0xFF;
		FREQ0 = (freq_reg >> 0) & 0xFF;
	}

private:
	Transport &transport_;
	CSPin cs_;
	MISOPin miso_;

	/**
	 * @brief Software reset using SRES command.
	 */
	void Reset()
	{
		cc1101_hard_reset(transport_, cs_, miso_);
	}

	// -------------------------------------------------------------------------
	// Default Initialization Table
	// -------------------------------------------------------------------------
	static constexpr RegisterValueTuple init_regs[] = {
		{ConfigurationRegister::IOCFG2, 0x06},
		{ConfigurationRegister::IOCFG0, 0x07},
		{ConfigurationRegister::SYNC1, 0x3D},
		{ConfigurationRegister::SYNC0, 0x0C},
		{ConfigurationRegister::PKTLEN, 0x3D},
		{ConfigurationRegister::PKTCTRL1, 0x0C},
		{ConfigurationRegister::PKTCTRL0, 0x05},
		{ConfigurationRegister::FSCTRL1, 0x06},
		{ConfigurationRegister::FSCTRL0, 0x00},

		// Frequency center frequency derived from CC1101::FREQ
		{ConfigurationRegister::FREQ2, static_cast<uint8_t>((FREQ >> 16) & 0xFF)},
		{ConfigurationRegister::FREQ1, static_cast<uint8_t>((FREQ >> 8) & 0xFF)},
		{ConfigurationRegister::FREQ0, static_cast<uint8_t>((FREQ >> 0) & 0xFF)},

		{ConfigurationRegister::MDMCFG4, 0xCA},
		{ConfigurationRegister::MDMCFG3, 0x83},
		{ConfigurationRegister::MDMCFG2, 0x13},
		{ConfigurationRegister::MDMCFG1, 0x72},
		{ConfigurationRegister::MDMCFG0, 0xF8},
		{ConfigurationRegister::DEVIATN, 0x34},
		{ConfigurationRegister::MCSM0, 0x18},
		{ConfigurationRegister::FOCCFG, 0x1D},
		{ConfigurationRegister::BSCFG, 0x6C},
		{ConfigurationRegister::AGCTRL2, 0x43},
		{ConfigurationRegister::AGCTRL1, 0x40},
		{ConfigurationRegister::AGCTRL0, 0x91},
		{ConfigurationRegister::FREND1, 0x56},
		{ConfigurationRegister::FREND0, 0x10},
		{ConfigurationRegister::FSCAL3, 0xE9},
		{ConfigurationRegister::FSCAL2, 0x2A},
		{ConfigurationRegister::FSCAL1, 0x00},
		{ConfigurationRegister::FSCAL0, 0x1F},
		{ConfigurationRegister::FSTEST, 0x59},
		{ConfigurationRegister::TEST2, 0x81},
		{ConfigurationRegister::TEST1, 0x35},
		{ConfigurationRegister::TEST0, 0x09},
	};
};

// -----------------------------------------------------------------------------
// CC1101 TI-Recommended Reset Sequence (requires CSn + MISO access)
// -----------------------------------------------------------------------------
template <typename Transport, typename CSPin, typename MISOPin>
void cc1101_hard_reset(Transport &t, const CSPin &cs, const MISOPin &miso)
{
	cs.high();
	HAL_Delay(1);

	cs.low();
	HAL_Delay(1);

	cs.high();
	HAL_Delay(1);

	cs.low();

	while (miso.read())
	{
	}

	uint8_t cmd = 0x30; // SRES
	t.write_reg(cmd, nullptr, 0);

	while (miso.read())
	{
	}
}
