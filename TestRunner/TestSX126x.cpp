#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "SX126x.hpp"

#include "mock_hal.h"

static SPI_HandleTypeDef hspi1;

// Mock GPIO port
static constexpr uint32_t MOCK_PORT_BASE = static_cast<uint32_t>(0xDEADBEEF); // value irrelevant for mock 
GPIO_TypeDef gpio_typedef;
GPIO_TypeDef* MOCK_PORT = &gpio_typedef;

constexpr static uint16_t GPIO_CS_PIN = GPIO_PIN_1;
constexpr static uint16_t GPIO_BUSY_PIN = GPIO_PIN_2;
constexpr static uint16_t GPIO_RESET_PIN = GPIO_PIN_3;
constexpr static uint16_t GPIO_DIO1_PIN = GPIO_PIN_4;


using MockCSPin = GpioPin<MOCK_PORT_BASE, GPIO_CS_PIN>;
using MockBusyPin = GpioPin<MOCK_PORT_BASE, GPIO_BUSY_PIN>;
using MockResetPin = GpioPin<MOCK_PORT_BASE, GPIO_RESET_PIN>;
using MockDio1Pin = GpioPin<MOCK_PORT_BASE, GPIO_DIO1_PIN>;

using SpiConfig = SPI_Stream_Config< hspi1, MockCSPin, 512>;

using SpiTransport = SPIStreamTransport<SpiConfig>;

static void clear_spi()
{
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();
}

struct Fixture
{
    MockBusyPin busy;
    MockResetPin reset;
    MockDio1Pin dio1;

    SpiConfig config;
    SpiTransport transport;

    using Radio = SX126x< SpiTransport, MockBusyPin, MockResetPin, MockDio1Pin>;

    Radio radio;

    Fixture() : transport(config), radio( transport, busy, reset, dio1) 
    { 
        init_spi_handle(&hspi1); clear_spi();
    }
};

TEST_CASE_FIXTURE(Fixture, "SetStandby")
{
    CHECK(radio.SetStandby());

    REQUIRE(get_spi_tx_buffer_count() == 2);

    auto* tx = get_spi_tx_buffer();

    CHECK(tx[0] == 0x80);
    CHECK(tx[1] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "SetPacketType_LoRa")
{
    CHECK(
        radio.SetPacketType( Radio::PacketType::LoRa));

    auto* tx = get_spi_tx_buffer();

    REQUIRE(get_spi_tx_buffer_count() == 2);

    CHECK(tx[0] == 0x8A);
    CHECK(tx[1] == 0x01);
}

TEST_CASE_FIXTURE(Fixture, "SetFrequency")
{
    radio.SetFrequency( 433000000);

    auto* tx = get_spi_tx_buffer();

    REQUIRE(get_spi_tx_buffer_count() == 5);

    uint32_t frf = ((uint64_t)433000000 << 25) / 32000000;
    CHECK(tx[0] == 0x86);

    CHECK(tx[1] == ((frf >> 24) & 0xFF));
    CHECK(tx[2] == ((frf >> 16) & 0xFF));
    CHECK(tx[3] == ((frf >> 8) & 0xFF));
    CHECK(tx[4] == ((frf >> 0) & 0xFF));
}

TEST_CASE_FIXTURE(Fixture, "SetTxPower")
{
    radio.SetTxPower(22);

    auto* tx = get_spi_tx_buffer();

    REQUIRE(get_spi_tx_buffer_count() == 3);

    CHECK(tx[0] == 0x8E);
    CHECK(tx[1] == 22);
    CHECK(tx[2] == 0x04);
}

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
    uint8_t payload[] { 0x11, 0x22, 0x33 };

    radio.WriteBuffer( 0, payload, sizeof(payload));

    auto* tx = get_spi_tx_buffer();

    REQUIRE(get_spi_tx_buffer_count() == 5);

    CHECK(tx[0] == 0x0E);
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x11);
    CHECK(tx[3] == 0x22);
    CHECK(tx[4] == 0x33);
}

TEST_CASE_FIXTURE(Fixture, "ReadBuffer") {
    uint8_t rx_mock[] { 0x00, 0x00, 0xAA, 0x11, 0x22, 0x33 };

    inject_spi_rx_data( rx_mock, sizeof(rx_mock));

    uint8_t data[3]{};

    CHECK( radio.ReadBuffer( 0, data, 3));

    CHECK(data[0] == 0x11);
    CHECK(data[1] == 0x22);
    CHECK(data[2] == 0x33);
}

TEST_CASE_FIXTURE(Fixture, "GetIrqStatus")
{
    uint8_t rx[] { 0x00, 0x00, 0x12, 0x34
    };

    inject_spi_rx_data( rx, sizeof(rx));

    uint16_t irq = radio.GetIrqStatus();

    CHECK(irq == 0x1234);
}

TEST_CASE_FIXTURE(Fixture, "ClearIrqStatus")
{
    radio.ClearIrqStatus( 0x5678);

    auto* tx = get_spi_tx_buffer();

    REQUIRE(get_spi_tx_buffer_count() == 3);

    CHECK(tx[0] == 0x02);
    CHECK(tx[1] == 0x56);
    CHECK(tx[2] == 0x78);
}

TEST_CASE_FIXTURE(Fixture, "Reset")
{
    set_gpio_pin_state(MOCK_PORT, GPIO_RESET_PIN, GPIO_PIN_SET);

    CHECK( radio.Reset());
    CHECK( get_gpio_pin_state(MOCK_PORT, GPIO_RESET_PIN) == true); 
}

TEST_CASE_FIXTURE(Fixture, "IsIRQAsserted")
{
    set_gpio_pin_state(MOCK_PORT, GPIO_DIO1_PIN, GPIO_PIN_RESET);
    CHECK_FALSE( radio.IsIRQAsserted());

    set_gpio_pin_state(MOCK_PORT, GPIO_DIO1_PIN, GPIO_PIN_SET);
    CHECK( radio.IsIRQAsserted());
}

TEST_CASE_FIXTURE(Fixture, "GetIrqStatus uses big-endian byte ordering")
{
    uint8_t rx[]
    {
        0x00, // SPI junk
        0x00, // status
        0xBE, // MSB
        0xEF  // LSB
    };

    inject_spi_rx_data(rx, sizeof(rx));

    uint16_t irq = radio.GetIrqStatus();

    CHECK(irq == 0xBEEF);
}

TEST_CASE_FIXTURE(Fixture, "GetIrqStatus does not swap bytes")
{
    uint8_t rx[] { 0x00, 0x00, 0x12, 0x34 };
    inject_spi_rx_data( rx, sizeof(rx));

    uint16_t irq = radio.GetIrqStatus();

    CHECK(irq == 0x1234);
    CHECK(irq != 0x3412);
}

TEST_CASE("sx126 frf sanity")
{
    uint32_t frf = ((uint64_t)433000000 << 25) / 32000000ULL;

    CHECK(frf == 0x1B100000);
}

TEST_CASE_FIXTURE(Fixture, "SetFrequency 433MHz exact encoding")
{
    radio.SetFrequency( 433000000);

    auto* tx = get_spi_tx_buffer();

    REQUIRE( get_spi_tx_buffer_count() == 5);

    CHECK(tx[0] == 0x86);

    CHECK(tx[1] == 0x1B);
    CHECK(tx[2] == 0x10);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "SetFrequency 868MHz exact encoding")
{
    radio.SetFrequency( 868000000);

    auto* tx = get_spi_tx_buffer();

    REQUIRE( get_spi_tx_buffer_count() == 5);

    CHECK(tx[0] == 0x86);

    CHECK(tx[1] == 0x36);
    CHECK(tx[2] == 0x40);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x00);
}

TEST_CASE_FIXTURE(Fixture, "SetFrequency 915MHz exact encoding")
{
    radio.SetFrequency( 915000000);

    auto* tx = get_spi_tx_buffer();

    REQUIRE( get_spi_tx_buffer_count()
        == 5);

    CHECK(tx[0] == 0x86);

    CHECK(tx[1] == 0x39);
    CHECK(tx[2] == 0x30);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0x00);
}