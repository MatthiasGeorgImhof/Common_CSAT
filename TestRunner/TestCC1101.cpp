#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "CC1101.hpp"
#include "mock_hal.h"
#include "GpioPin.hpp"

// -----------------------------------------------------------------------------
// Test Setup & Globals
// -----------------------------------------------------------------------------

static SPI_HandleTypeDef hspi1;

// Fake GPIO port addresses (NTTPs for GpioPin)
constexpr uint32_t MOCK_PORT_B = 0x1243;

// Real GPIO_TypeDef objects for the mock HAL
GPIO_TypeDef mock_port_b = {};

static constexpr uint16_t CS_PIN   = GPIO_PIN_10;
static constexpr uint16_t MISO_PIN = GPIO_PIN_11;

// Configure the Transport Layer
using CC1101_Config    = SPI_Register_Config<hspi1, CS_PIN, 128, 100>;
using CC1101_Transport = SPIRegisterTransport<CC1101_Config>;

// GPIO pins as types
using CSPin  = GpioPin<MOCK_PORT_B, CS_PIN>;
using MISOPin = GpioPin<MOCK_PORT_B, MISO_PIN>;

// Alias for the radio type
using Radio = CC1101<CC1101_Transport, CSPin, MISOPin>;

struct CC1101Fixture {
    CC1101_Config    config;
    CC1101_Transport transport;
    Radio            radio;

    CC1101Fixture()
        : config(&mock_port_b),
          transport(config),
          radio(transport, CSPin{}, MISOPin{})
    {
        clear_spi_tx_buffer();
        clear_spi_rx_buffer();
        reset_gpio_port_state(&mock_port_b);
        init_spi_handle(&hspi1);

        CSPin{}.high();   // ensure CS starts high
    }
};

// -----------------------------------------------------------------------------
// Test Cases
// -----------------------------------------------------------------------------

TEST_CASE_FIXTURE(CC1101Fixture, "Write Register: Correct SPI sequence and CS toggling") {
    uint8_t value = 0x42;
    radio.WriteConfiguration(Radio::ConfigurationRegister::PKTCTRL0, value);

    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(get_spi_tx_buffer()[0] == 0x08);
    CHECK(get_spi_tx_buffer()[1] == 0x42);

    CHECK(get_gpio_pin_state(&mock_port_b, CS_PIN) == GPIO_PIN_SET);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Read Register: Correct Address header with Read Bit") {
    uint8_t mock_val = 0x55;
    inject_spi_rx_data(&mock_val, 1);

    uint8_t result = radio.ReadConfiguration(Radio::ConfigurationRegister::CHANNR);

    CHECK(get_spi_tx_buffer()[0] == 0x8A);
    CHECK(result == 0x55);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Strobe Command: Single byte transmission") {
    radio.Strobe(Radio::StrobeCommand::SFTX);

    REQUIRE(get_spi_tx_buffer_count() == 1);
    CHECK(get_spi_tx_buffer()[0] == 0x3B);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Burst Write: Correct header and multi-byte sequence") {
    uint8_t data[] = {0x01, 0x02, 0x03};
    radio.WriteBurst(Radio::BurstRegister::TXFIFO, data, 3);

    REQUIRE(get_spi_tx_buffer_count() == 4);
    CHECK(get_spi_tx_buffer()[0] == 0x7F);
    CHECK(get_spi_tx_buffer()[1] == 0x01);
    CHECK(get_spi_tx_buffer()[2] == 0x02);
    CHECK(get_spi_tx_buffer()[3] == 0x03);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Status Register Read: Correct Read/Burst bits") {
    uint8_t state = 0x01;
    inject_spi_rx_data(&state, 1);

    uint8_t result = radio.ReadStatus(Radio::StatusRegister::MARCSTATE);

    CHECK(get_spi_tx_buffer()[0] == 0xF5);
    CHECK(result == 0x01);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Frequency Calculation: Verify math for 433.92MHz") {
    radio.setCarrierFrequency(433920000);

    uint8_t* tx = get_spi_tx_buffer();
    size_t len = get_spi_tx_buffer_count();

    CHECK(tx[len-1] == 0x71);
    CHECK(tx[len-2] == 0x0F);

    CHECK(tx[len-3] == 0xB0);
    CHECK(tx[len-4] == 0x0E);

    CHECK(tx[len-5] == 0x10);
    CHECK(tx[len-6] == 0x0D);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Initialization Sequence: Verify Reset and Final State") {
    radio.Init();

    uint8_t* tx = get_spi_tx_buffer();
    size_t count = get_spi_tx_buffer_count();

    CHECK(tx[0] == 0x30);
    CHECK(tx[count-1] == 0x39);

    bool found_pktlen = false;
    for(size_t i=0; i < count-1; ++i) {
        if(tx[i] == 0x06 && tx[i+1] == 0x3D) found_pktlen = true;
    }
    CHECK(found_pktlen);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Power Setting: Verify PATABLE access") {
    radio.SetPower(Radio::TXPower::LOW);

    uint8_t* tx = get_spi_tx_buffer();
    size_t len = get_spi_tx_buffer_count();

    CHECK(tx[len-2] == 0x3E);
    CHECK(tx[len-1] == 0x03);
}
