#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "MR25H10.hpp"
#include "mock_hal.h"
#include "Transport.hpp"

SPI_HandleTypeDef mock_spi;
GPIO_TypeDef mock_gpio = {};
static constexpr uint32_t MOCK_GPIO_BASE = static_cast<uint32_t>(0xDEADBEEF); // value irrelevant for mock 
auto constexpr CS_PIN = GPIO_PIN_5; // Pin 5 for CS

// Configure the Transport Layer
using CS_Pin = GpioPin<MOCK_GPIO_BASE, CS_PIN>;
using Config = SPI_Stream_Config<mock_spi, CS_Pin, 128>;
using Transport = SPIStreamTransport<Config>;
using SRAM = MR25H10<Transport>;

TEST_CASE("MR25H10 readStatus returns correct value") {
    clear_spi_rx_buffer();

    uint8_t status = 0xAC;
    inject_spi_rx_data(&status, 1);

    Config config;
    Transport transport(config);
    SRAM sram(transport);

    auto result = sram.readStatus();
    REQUIRE(result.has_value());
    CHECK(result.value() == status);
}

TEST_CASE("MR25H10 writeStatus sends correct command and data") {
    clear_spi_tx_buffer();

    Config config;
    Transport transport(config);
    SRAM sram(transport);

    uint8_t status = 0x5A;
    CHECK(sram.writeStatus(status) == true);

    auto tx = get_spi_tx_buffer();
    CHECK(get_spi_tx_buffer_count() == 2);
    CHECK(tx[0] == static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_WRSR));
    CHECK(tx[1] == status);
}

TEST_CASE("MR25H10 readBytes sends correct header and reads payload") {
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();

    uint8_t expected_payload[3] = { 0x11, 0x22, 0x33 };
    inject_spi_rx_data(expected_payload, 3);

    Config config;
    Transport transport(config);
    SRAM sram(transport);

    uint8_t buffer[3] = {};
    CHECK(sram.readBytes(0x001234, buffer, 3) == true);

    // Validate header
    auto tx = get_spi_tx_buffer();
    CHECK(tx[0] == static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_READ));
    CHECK(tx[1] == 0x00);
    CHECK(tx[2] == 0x12);
    CHECK(tx[3] == 0x34);

    // Validate payload
    CHECK(buffer[0] == 0x11);
    CHECK(buffer[1] == 0x22);
    CHECK(buffer[2] == 0x33);
}

TEST_CASE("MR25H10 writeBytes sends WREN, header, and payload") {
    clear_spi_tx_buffer();

    Config config;
    Transport transport(config);
    SRAM sram(transport);

    uint8_t payload[2] = { 0xAA, 0xBB };
    CHECK(sram.writeBytes(0x0000F0, payload, 2) == true);

    auto tx = get_spi_tx_buffer();

    // WREN
    CHECK(tx[0] == static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_WREN));

    // WRITE header
    CHECK(tx[1] == static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_WRITE));
    CHECK(tx[2] == 0x00);
    CHECK(tx[3] == 0x00);
    CHECK(tx[4] == 0xF0);

    // Payload
    CHECK(tx[5] == 0xAA);
    CHECK(tx[6] == 0xBB);
}
