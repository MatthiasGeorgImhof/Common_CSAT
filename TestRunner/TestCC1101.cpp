#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "CC1101.hpp"
#include "mock_hal.h"

// -----------------------------------------------------------------------------
// Test Setup & Globals
// -----------------------------------------------------------------------------

static SPI_HandleTypeDef hspi1;
static GPIO_TypeDef MockGPIOB;
static constexpr uint16_t CS_PIN = GPIO_PIN_10;

// Configure the Transport Layer
// HandleRef: hspi1, Pin: 10, MaxSize: 128, Timeout: 100
using CC1101_Config = SPI_Register_Config<hspi1, CS_PIN, 128, 100>;
using CC1101_Transport = SPIRegisterTransport<CC1101_Config>;

struct CC1101Fixture {
    CC1101_Config config;
    CC1101_Transport transport;
    CC1101<CC1101_Transport> radio;

    CC1101Fixture() 
        : config(&MockGPIOB), 
          transport(config), 
          radio(transport) 
    {
        // Reset Mock Hardware State
        clear_spi_tx_buffer();
        clear_spi_rx_buffer();
        reset_gpio_port_state(&MockGPIOB);
        init_spi_handle(&hspi1);
        
        // Ensure CS starts High (idle)
        set_gpio_pin_state(&MockGPIOB, CS_PIN, GPIO_PIN_SET);
    }
};

// -----------------------------------------------------------------------------
// Test Cases
// -----------------------------------------------------------------------------

TEST_CASE_FIXTURE(CC1101Fixture, "Write Register: Correct SPI sequence and CS toggling") {
    uint8_t value = 0x42;
    radio.WriteConfiguration(CC1101<CC1101_Transport>::ConfigurationRegister::PKTCTRL0, value);

    // 1. Verify SPI Buffer: [Address (0x08), Value (0x42)]
    REQUIRE(get_spi_tx_buffer_count() == 2);
    CHECK(get_spi_tx_buffer()[0] == 0x08);
    CHECK(get_spi_tx_buffer()[1] == 0x42);

    // 2. Verify CS was left in High state (Deselected) after transaction
    CHECK(get_gpio_pin_state(&MockGPIOB, CS_PIN) == GPIO_PIN_SET);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Read Register: Correct Address header with Read Bit") {
    // Inject mock data to be returned by SPI
    // SPI Transport Read sends 1 byte address, then N bytes dummy to receive
    // We need to inject the dummy byte response
    uint8_t mock_val = 0x55;
    inject_spi_rx_data(&mock_val, 1); 

    uint8_t result = radio.ReadConfiguration(CC1101<CC1101_Transport>::ConfigurationRegister::CHANNR);

    // CHANNR is 0x0A. Read bit is 0x80. Header = 0x8A.
    CHECK(get_spi_tx_buffer()[0] == 0x8A);
    CHECK(result == 0x55);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Strobe Command: Single byte transmission") {
    radio.Strobe(CC1101<CC1101_Transport>::StrobeCommand::SFTX);

    // SFTX is 0x3B. Length should be exactly 1 byte.
    REQUIRE(get_spi_tx_buffer_count() == 1);
    CHECK(get_spi_tx_buffer()[0] == 0x3B);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Burst Write: Correct header and multi-byte sequence") {
    uint8_t data[] = {0x01, 0x02, 0x03};
    // TXFIFO (0x3F) | BURST (0x40) = 0x7F
    radio.WriteBurst(CC1101<CC1101_Transport>::BurstRegister::TXFIFO, data, 3);

    REQUIRE(get_spi_tx_buffer_count() == 4);
    CHECK(get_spi_tx_buffer()[0] == 0x7F);
    CHECK(get_spi_tx_buffer()[1] == 0x01);
    CHECK(get_spi_tx_buffer()[2] == 0x02);
    CHECK(get_spi_tx_buffer()[3] == 0x03);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Status Register Read: Correct Read/Burst bits") {
    // MARCSTATE is 0x35. 
    // Header must be 0x35 (Addr) | 0x80 (Read) | 0x40 (Burst) = 0xF5
    uint8_t state = 0x01; 
    inject_spi_rx_data(&state, 1);

    uint8_t result = radio.ReadStatus(CC1101<CC1101_Transport>::StatusRegister::MARCSTATE);

    // 0xF5 is 245 decimal
    CHECK(get_spi_tx_buffer()[0] == 0xF5); 
    CHECK(result == 0x01);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Frequency Calculation: Verify math for 433.92MHz") {
    radio.setCarrierFrequency(433920000);

    uint8_t* tx = get_spi_tx_buffer();
    size_t len = get_spi_tx_buffer_count();

    // Check last written values (FREQ0)
    // 0x71 is 113 decimal
    CHECK(tx[len-1] == 0x71); 
    CHECK(tx[len-2] == 0x0F); // FREQ0 Register Addr
    
    // 0xB0 is 176 decimal
    CHECK(tx[len-3] == 0xB0); 
    CHECK(tx[len-4] == 0x0E); // FREQ1 Register Addr
    
    // 0x10 is 16 decimal
    CHECK(tx[len-5] == 0x10); 
    CHECK(tx[len-6] == 0x0D); // FREQ2 Register Addr
}

TEST_CASE_FIXTURE(CC1101Fixture, "Initialization Sequence: Verify Reset and Final State") {
    radio.Init();

    uint8_t* tx = get_spi_tx_buffer();
    size_t count = get_spi_tx_buffer_count();

    // 1. Must start with SRES (0x30)
    CHECK(tx[0] == 0x30);

    // 2. Must end with SPWD (0x39) to stay in low power
    CHECK(tx[count-1] == 0x39);

    // 3. Verify specific config from table was applied
    // PKTLEN (0x06) should be 0x3D
    bool found_pktlen = false;
    for(size_t i=0; i < count-1; ++i) {
        if(tx[i] == 0x06 && tx[i+1] == 0x3D) found_pktlen = true;
    }
    CHECK(found_pktlen);
}

TEST_CASE_FIXTURE(CC1101Fixture, "Power Setting: Verify PATABLE access") {
    radio.SetPower(CC1101<CC1101_Transport>::TXPower::LOW);
    
    // PATABLE is 0x3E. Value for LOW is 0x03.
    uint8_t* tx = get_spi_tx_buffer();
    size_t len = get_spi_tx_buffer_count();
    
    CHECK(tx[len-2] == 0x3E);
    CHECK(tx[len-1] == 0x03);
}