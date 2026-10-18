#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest/doctest.h"

#include "W5500Driver.hpp"
#include "Transport.hpp"
#include "mock_hal.h"

SPI_HandleTypeDef hspi1;
GPIO_TypeDef gpioa;
static constexpr uint32_t MOCK_PORT_BASE = static_cast<uint32_t>(0xDEADBEEF); // value irrelevant for mock 

constexpr static StaticAddress NETWORK {
   MacAddress{0x02,0x00,0x00,0x00,0x00,0x01},
   IPv4Address{192,42,172,240},
   IPv4Address{255,255,255,0},
   IPv4Address{192,42,172,1},
   IPv4Address{192,42,172,1}
};

auto constexpr CS_PIN = GPIO_PIN_5; // Pin 5 for CS
auto constexpr RESET_PIN = GPIO_PIN_11; // Pin 11 for Reset

using EthernetCsPin  = GpioPin<MOCK_PORT_BASE, CS_PIN>;
using EthernetResetPin  = GpioPin<MOCK_PORT_BASE, RESET_PIN>;
using EthernetConfig = SPI_Stream_Config<hspi1, EthernetCsPin, 128>;
using EthernetTransport = SPIStreamTransport<EthernetConfig>;
using Ethernet = W5500Driver<EthernetTransport, EthernetResetPin, NETWORK>;

struct W5500Fixture
{
    EthernetConfig config;
    EthernetTransport transport;
    Ethernet ethernet;

    W5500Fixture() : transport(config), ethernet(transport)
    {
        clear_spi_tx_buffer();
        clear_spi_rx_buffer();

        reset_gpio_port_state(&gpioa);

        init_spi_handle(&hspi1);
    }
};

TEST_CASE_FIXTURE(W5500Fixture, "SPI byte write callback goes through transport")
{
    ethernet.wizchipWriteByte(0x42);

    REQUIRE(get_spi_tx_buffer_count() == 1);

    CHECK(get_spi_tx_buffer()[0] == 0x42);
}

TEST_CASE_FIXTURE(W5500Fixture, "SPI byte read callback goes through transport")
{
    uint8_t injected = 0xAB;

    inject_spi_rx_data(
        &injected,
        sizeof(injected));

    uint8_t value =
        ethernet.wizchipReadByte();

    CHECK(value == 0xAB);
}

TEST_CASE_FIXTURE(W5500Fixture, "SPI burst write callback goes through transport")
{
    uint8_t tx[] = { 0x11, 0x22, 0x33, 0x44 };

    ethernet.wizchipWriteBurst(tx, sizeof(tx));

    REQUIRE(get_spi_tx_buffer_count() == sizeof(tx));

    CHECK(memcmp(get_spi_tx_buffer(), tx, sizeof(tx)) == 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "SPI burst read callback goes through transport")
{
    uint8_t injected[] = { 0xAA, 0xBB, 0xCC };

    inject_spi_rx_data(injected, sizeof(injected));

    uint8_t rx[3] = {};

    ethernet.wizchipReadBurst(rx, sizeof(rx));

    CHECK(memcmp(rx, injected, sizeof(rx)) == 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "Chip select callback controls transport")
{
    ethernet.wizchipSelect();

    CHECK(get_gpio_pin_state(&gpioa, GPIO_PIN_5) == GPIO_PIN_RESET);

    ethernet.wizchipDeselect();

    CHECK(get_gpio_pin_state( &gpioa, GPIO_PIN_5) == GPIO_PIN_SET);
}

TEST_CASE_FIXTURE(W5500Fixture, "Single byte write forwards to transport")
{
    ethernet.wizchipWriteByte(0x42);

    REQUIRE(get_spi_tx_buffer_count() == 1);
    CHECK(get_spi_tx_buffer()[0] == 0x42);
}

TEST_CASE_FIXTURE(W5500Fixture, "Burst write forwards to transport")
{
    const uint8_t tx[] = {0x11, 0x22, 0x33, 0x44};

    ethernet.wizchipWriteBurst(
        const_cast<uint8_t*>(tx),
        sizeof(tx));

    REQUIRE(get_spi_tx_buffer_count() == sizeof(tx));

    CHECK(
        memcmp(
            get_spi_tx_buffer(),
            tx,
            sizeof(tx)) == 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "Burst read forwards to transport")
{
    uint8_t expected[] = {0xAA, 0xBB, 0xCC};

    inject_spi_rx_data(expected, sizeof(expected));

    uint8_t actual[sizeof(expected)]{};

    ethernet.wizchipReadBurst(
        actual,
        sizeof(actual));

    CHECK(
        memcmp(
            actual,
            expected,
            sizeof(expected)) == 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "Driver can be constructed")
{
    // Construction is performed by the fixture.
    // Verify that the objects exist and are wired together.

    CHECK(&config != nullptr);
    CHECK(&transport != nullptr);
    CHECK(&ethernet != nullptr);
}


