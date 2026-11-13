#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "SX126x.hpp"
#include "mock_hal.h"

static SPI_HandleTypeDef hspi1;

// Mock GPIO port
static constexpr uint32_t MOCK_PORT_BASE = static_cast<uint32_t>(0xDEADBEEF);
GPIO_TypeDef gpio_typedef;
GPIO_TypeDef* MOCK_PORT = &gpio_typedef;

constexpr static uint16_t GPIO_CS_PIN    = GPIO_PIN_1;
constexpr static uint16_t GPIO_BUSY_PIN  = GPIO_PIN_2;
constexpr static uint16_t GPIO_RESET_PIN = GPIO_PIN_3;
constexpr static uint16_t GPIO_DIO1_PIN  = GPIO_PIN_4;

using MockCSPin    = GpioPin<MOCK_PORT_BASE, GPIO_CS_PIN>;
using MockBusyPin  = GpioPin<MOCK_PORT_BASE, GPIO_BUSY_PIN>;
using MockResetPin = GpioPin<MOCK_PORT_BASE, GPIO_RESET_PIN>;
using MockDio1Pin  = GpioPin<MOCK_PORT_BASE, GPIO_DIO1_PIN>;

using SpiConfig    = SPI_Stream_Config<hspi1, MockCSPin, 512>;
using SpiTransport = SPIStreamTransport<SpiConfig>;

static void clear_spi()
{
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();
}

struct Fixture
{
    MockBusyPin  busy;
    MockResetPin reset;
    MockDio1Pin  dio1;

    SpiConfig    config;
    SpiTransport transport;

    using Radio = SX126x<SpiTransport, MockBusyPin, MockResetPin, MockDio1Pin>;
    Radio radio;

    Fixture() : transport(config), radio(transport)
    {
        init_spi_handle(&hspi1);
        clear_spi();
    }
};

// ============================================================================
// Initialization & Chip Control Tests
// ============================================================================

TEST_CASE_FIXTURE(Fixture, "Reset")
{
    set_gpio_pin_state(MOCK_PORT, GPIO_RESET_PIN, GPIO_PIN_SET);
    CHECK(radio.Reset());
    CHECK(get_gpio_pin_state(MOCK_PORT, GPIO_RESET_PIN) == true);
}

TEST_CASE_FIXTURE(Fixture, "Init sends SetStandby after reset")
{
    CHECK(radio.Init());

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(tx[0] == 0x80); // Command::SetStandby
    CHECK(tx[1] == 0x00); // STDBY_RC
}

TEST_CASE_FIXTURE(Fixture, "SetStandby")
{
    CHECK(radio.SetStandby());

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(tx[0] == 0x80);
    CHECK(tx[1] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "GetStatus")
{
    // ReadCommand expects 2 dummy/status bytes, data starts at rx[2]
    uint8_t rx_mock[] = { 0x00, 0x00, 0x2A };
    inject_spi_rx_data(rx_mock, sizeof(rx_mock));

    uint8_t status = radio.GetStatus();

    CHECK(status == 0x2A);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 3);
    CHECK(tx[0] == 0xC0); // Command::GetStatus
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "GetDeviceErrors big-endian decoding")
{
    // Mock SPI: Opcode (0x17) -> Status -> Error MSB (0x01) -> Error LSB (0x40)
    uint8_t rx_mock[] = { 0x00, 0x00, 0x01, 0x40 };
    inject_spi_rx_data(rx_mock, sizeof(rx_mock));

    uint16_t errors = radio.GetDeviceErrors();

    CHECK(errors == 0x0140);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 4);
    CHECK(tx[0] == 0x17); // Command::GetDeviceErrors
}

TEST_CASE_FIXTURE(Fixture, "ClearDeviceErrors")
{
    radio.ClearDeviceErrors();

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 3);
    CHECK(tx[0] == 0x07); // Command::ClearDeviceErrors
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x00);
}

// ============================================================================
// Radio Configuration Tests
// ============================================================================

TEST_CASE_FIXTURE(Fixture, "SetPacketType_LoRa")
{
    CHECK(radio.SetPacketType(sx126x::PacketType::LoRa));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(tx[0] == 0x8A);
    CHECK(tx[1] == 0x01);
}

TEST_CASE_FIXTURE(Fixture, "SetPacketType_GFSK")
{
    CHECK(radio.SetPacketType(sx126x::PacketType::GFSK));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(tx[0] == 0x8A);
    CHECK(tx[1] == 0x00);
}

TEST_CASE("sx126 frf calculation sanity")
{
    uint32_t frf = ((uint64_t)433000000 << 25) / 32000000ULL;
    CHECK(frf == 0x1B100000);
}

TEST_CASE_FIXTURE(Fixture, "SetFrequency 433MHz exact big-endian encoding")
{
    radio.SetFrequency(433000000);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x86);
    CHECK(tx[1] == 0x1B);
    CHECK(tx[2] == 0x10);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "SetFrequency 868MHz exact big-endian encoding")
{
    radio.SetFrequency(868000000);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x86);
    CHECK(tx[1] == 0x36);
    CHECK(tx[2] == 0x40);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "SetFrequency 915MHz exact big-endian encoding")
{
    radio.SetFrequency(915000000);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x86);
    CHECK(tx[1] == 0x39);
    CHECK(tx[2] == 0x30);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "SetTxPower")
{
    radio.SetTxPower(22);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 3);
    CHECK(tx[0] == 0x8E); // Command::SetTxParams
    CHECK(tx[1] == 22);   // Power dBm
    CHECK(tx[2] == 0x04); // Ramp time
}

TEST_CASE_FIXTURE(Fixture, "SetBufferBaseAddress")
{
    CHECK(radio.SetBufferBaseAddress(0x10, 0x90));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 3);
    CHECK(tx[0] == 0x8F); // Command::SetBufferBaseAddress
    CHECK(tx[1] == 0x10); // TX Base
    CHECK(tx[2] == 0x90); // RX Base
}

TEST_CASE_FIXTURE(Fixture, "SetModulationParams")
{
    sx126x::LoRaModulationParams params{
        .sf   = 7,
        .bw   = 4,
        .cr   = 1,
        .ldro = 0
    };

    CHECK(radio.SetModulationParams(params));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x8B); // Command::SetModulationParams
    CHECK(tx[1] == 7);
    CHECK(tx[2] == 4);
    CHECK(tx[3] == 1);
    CHECK(tx[4] == 0);
}

TEST_CASE_FIXTURE(Fixture, "SetPacketParams big-endian preamble swapping")
{
    sx126x::LoRaPacketParams params{
        .preamble_length = 0x0280, // 640 symbols -> MSB: 0x02, LSB: 0x80
        .header_type     = sx126x::HeaderType::Variable,
        .payload_length  = 0x14,
        .crc             = 0x01,
        .iq              = 0x00
    };

    CHECK(radio.SetPacketParams(params));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 7);
    CHECK(tx[0] == 0x8C); // Command::SetPacketParams
    CHECK(tx[1] == 0x02); // Preamble Length MSB
    CHECK(tx[2] == 0x80); // Preamble Length LSB
    CHECK(tx[3] == 0x00); // Header Type
    CHECK(tx[4] == 0x14); // Payload Length
    CHECK(tx[5] == 0x01); // CRC
    CHECK(tx[6] == 0x00); // IQ
}

TEST_CASE_FIXTURE(Fixture, "SetPaConfig")
{
    sx126x::PaConfig params{
        .paDutyCycle = 0x04,
        .hpMax       = 0x07,
        .deviceSel   = 0x00,
        .paLut       = 0x01
    };

    CHECK(radio.SetPaConfig(params));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x95); // Command::SetPaConfig
    CHECK(tx[1] == 0x04);
    CHECK(tx[2] == 0x07);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x01);
}

TEST_CASE_FIXTURE(Fixture, "SetDio2AsRfSwitchCtrl enable and disable")
{
    CHECK(radio.SetDio2AsRfSwitchCtrl(true));
    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(tx[0] == 0x9D);
    CHECK(tx[1] == 0x01);

    clear_spi();

    CHECK(radio.SetDio2AsRfSwitchCtrl(false));
    tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(tx[0] == 0x9D);
    CHECK(tx[1] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "SetDio3AsTcxoCtrl 24-bit big-endian timeout")
{
    // timeoutTicks = 0x010203 (Bits [23:16]=0x01, [15:8]=0x02, [7:0]=0x03)
    CHECK(radio.SetDio3AsTcxoCtrl(0x07, 0x010203));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x97); // Command::SetDio3AsTcxoCtrl
    CHECK(tx[1] == 0x07); // 3.3V
    CHECK(tx[2] == 0x01); // MSB
    CHECK(tx[3] == 0x02); // Mid
    CHECK(tx[4] == 0x03); // LSB
}

// ============================================================================
// TX, RX, Buffer Access Tests
// ============================================================================

TEST_CASE_FIXTURE(Fixture, "StartRx")
{
    radio.StartRx();

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 4);
    CHECK(tx[0] == 0x82);
    CHECK(tx[1] == 0xFF);
    CHECK(tx[2] == 0xFF);
    CHECK(tx[3] == 0xFF);
}

TEST_CASE_FIXTURE(Fixture, "StartTx")
{
    radio.StartTx();

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 4);
    CHECK(tx[0] == 0x83);
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x00);
    CHECK(tx[3] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "WriteBuffer")
{
    uint8_t payload[]{ 0x11, 0x22, 0x33 };

    radio.WriteBuffer(0x10, payload, sizeof(payload));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x0E); // Command::WriteBuffer
    CHECK(tx[1] == 0x10); // Offset
    CHECK(tx[2] == 0x11);
    CHECK(tx[3] == 0x22);
    CHECK(tx[4] == 0x33);
}

TEST_CASE_FIXTURE(Fixture, "ReadBuffer")
{
    uint8_t rx_mock[]{ 0x00, 0x00, 0xAA, 0x11, 0x22, 0x33 };
    inject_spi_rx_data(rx_mock, sizeof(rx_mock));

    uint8_t data[3]{};
    CHECK(radio.ReadBuffer(0x20, data, 3));

    CHECK(data[0] == 0x11);
    CHECK(data[1] == 0x22);
    CHECK(data[2] == 0x33);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 6);
    CHECK(tx[0] == 0x1E); // Command::ReadBuffer
    CHECK(tx[1] == 0x20); // Offset
}

// ============================================================================
// IRQ & GPIO Tests
// ============================================================================

TEST_CASE_FIXTURE(Fixture, "SetDioIrqParams all masks big-endian swapped")
{
    sx126x::IrqMasks masks{
        .IrqMask  = static_cast<sx126x::IRQ>(0x0123),
        .Dio1Mask = static_cast<sx126x::IRQ>(0x0456),
        .Dio2Mask = static_cast<sx126x::IRQ>(0x0789),
        .Dio3Mask = static_cast<sx126x::IRQ>(0x0ABC)
    };

    CHECK(radio.SetDioIrqParams(masks));

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 9);
    CHECK(tx[0] == 0x08); // Command::SetDioIrqParams

    // IrqMask: 0x0123
    CHECK(tx[1] == 0x01);
    CHECK(tx[2] == 0x23);

    // Dio1Mask: 0x0456
    CHECK(tx[3] == 0x04);
    CHECK(tx[4] == 0x56);

    // Dio2Mask: 0x0789
    CHECK(tx[5] == 0x07);
    CHECK(tx[6] == 0x89);

    // Dio3Mask: 0x0ABC
    CHECK(tx[7] == 0x0A);
    CHECK(tx[8] == 0xBC);
}

TEST_CASE_FIXTURE(Fixture, "GetIrqStatus big-endian decoding")
{
    uint8_t rx[]{ 0x00, 0x00, 0xBE, 0xEF };
    inject_spi_rx_data(rx, sizeof(rx));

    uint16_t irq = radio.GetIrqStatus();

    CHECK(irq == 0xBEEF);
    CHECK(irq != 0xEFBE);
}

TEST_CASE_FIXTURE(Fixture, "ClearIrqStatus big-endian encoding")
{
    radio.ClearIrqStatus(0x5678);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 3);
    CHECK(tx[0] == 0x02); // Command::ClearIrqStatus
    CHECK(tx[1] == 0x56); // MSB
    CHECK(tx[2] == 0x78); // LSB
}

TEST_CASE_FIXTURE(Fixture, "IsIRQAsserted checks DIO1 pin state")
{
    set_gpio_pin_state(MOCK_PORT, GPIO_DIO1_PIN, GPIO_PIN_RESET);
    CHECK_FALSE(radio.IsIRQAsserted());

    set_gpio_pin_state(MOCK_PORT, GPIO_DIO1_PIN, GPIO_PIN_SET);
    CHECK(radio.IsIRQAsserted());
}

// ============================================================================
// Reception Status Tests
// ============================================================================

TEST_CASE_FIXTURE(Fixture, "GetRxBufferStatus")
{
    // MOSI sends: 0x13 (Opcode) + 3 dummy NOPs
    // MISO returns: Status (0x00) -> Dummy (0x00) -> PayloadLength (24) -> BufferPointer (0x80)
    uint8_t rx_mock[] = { 0x00, 0x00, 24, 0x80 };
    inject_spi_rx_data(rx_mock, sizeof(rx_mock));

    sx126x::RxBufferStatus rx_status = radio.GetRxBufferStatus();

    CHECK(rx_status.payload_length == 24);
    CHECK(rx_status.buffer_pointer == 0x80);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 4);
    CHECK(tx[0] == 0x13); // Command::GetRxBufferStatus
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x00);
    CHECK(tx[3] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "GetPacketStatus with positive SNR")
{
    // Raw values:
    // RssiPkt       = 140 -> -140 / 2 = -70 dBm
    // SnrPkt        = 32  ->   32 / 4 =  +8 dB
    // SignalRssiPkt = 146 -> -146 / 2 = -73 dBm
    uint8_t rx_mock[] = { 0x00, 0x00, 140, 32, 146 };
    inject_spi_rx_data(rx_mock, sizeof(rx_mock));

    sx126x::PacketStatus pkt_status = radio.GetPacketStatus();

    CHECK(pkt_status.rssi_dbm == -70);
    CHECK(pkt_status.snr_db == 8);
    CHECK(pkt_status.signal_rssi_dbm == -73);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 5);
    CHECK(tx[0] == 0x14); // Command::GetPacketStatus
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x00);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "GetPacketStatus with negative SNR (2's complement)")
{
    // Raw values:
    // RssiPkt       = 200 -> -200 / 2 = -100 dBm
    // SnrPkt        = 0xF0 (-16 as signed int8_t) -> -16 / 4 = -4 dB
    // SignalRssiPkt = 210 -> -210 / 2 = -105 dBm
    uint8_t rx_mock[] = { 0x00, 0x00, 200, 0xF0, 210 };
    inject_spi_rx_data(rx_mock, sizeof(rx_mock));

    sx126x::PacketStatus pkt_status = radio.GetPacketStatus();

    CHECK(pkt_status.rssi_dbm == -100);
    CHECK(pkt_status.snr_db == -4);
    CHECK(pkt_status.signal_rssi_dbm == -105);
}

TEST_CASE_FIXTURE(Fixture, "GetRssiInst")
{
    // Raw value: 154 -> -154 / 2 = -77 dBm
    uint8_t rx_mock[] = { 0x00, 0x00, 154 };
    inject_spi_rx_data(rx_mock, sizeof(rx_mock));

    int16_t rssi = radio.GetRssiInst();

    CHECK(rssi == -77);

    auto* tx = get_spi_tx_buffer();
    REQUIRE(get_spi_tx_buffer_count() == 3);
    CHECK(tx[0] == 0x15); // Command::GetRssiInst
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x00);
}