#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest/doctest.h"

#ifndef _WIZCHIP_
#define _WIZCHIP_ W5500
#endif

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

#include "W5500Driver.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "Transport.hpp"
#include "mock_hal.h"

SPI_HandleTypeDef hspi1;
GPIO_TypeDef gpioa;

using EthernetConfig = SPI_Stream_Config<hspi1, GPIO_PIN_5, 128>;
using EthernetTransport = SPIStreamTransport<EthernetConfig>;
using Ethernet = W5500Driver<EthernetTransport>;

// =============================================================================
// Lightweight W5500 Register & Buffer Emulator
// =============================================================================
struct MockW5500Device
{
    uint8_t  sn_mr   = Sn_MR_UDP;
    uint8_t  sn_sr   = SOCK_UDP;
    uint8_t  sn_ir   = Sn_IR_SENDOK;
    uint16_t sn_rx_rsr = 0;
    uint8_t  phycfgr = 0x01; // PHY link up

    // RX buffer memory for Socket 0
    uint8_t  rx_mem[128] = {};
    uint16_t rx_mem_pos = 0;

    // SPI frame state
    uint8_t cmd[3] = {};
    uint8_t cmd_count = 0;

    void reset()
    {
        sn_mr   = Sn_MR_UDP;
        sn_sr   = SOCK_UDP;
        sn_ir   = Sn_IR_SENDOK;
        sn_rx_rsr = 0;
        phycfgr = 0x01;
        rx_mem_pos = 0;
        cmd_count = 0;
        std::memset(rx_mem, 0, sizeof(rx_mem));
    }

    void select()
    {
        cmd_count = 0;
    }

    void deselect()
    {
        cmd_count = 0;
    }

    void write_byte(uint8_t b)
    {
        if (cmd_count < 3)
        {
            cmd[cmd_count++] = b;
        }
        else
        {
            // If writing to Socket 0 Control Register (Sn_CR), simulate state transitions
            uint16_t addr  = static_cast<uint16_t>((static_cast<uint16_t>(cmd[0]) << 8) | cmd[1]);
            uint8_t  block = static_cast<uint8_t>((cmd[2] >> 3) & 0x1F);
            if (block == 0x01 && addr == 0x0001)
            {
                if (b == Sn_CR_OPEN)  sn_sr = SOCK_UDP;
                if (b == Sn_CR_CLOSE) sn_sr = SOCK_CLOSED;
            }
        }
    }

    uint8_t read_byte()
    {
        uint16_t addr  = static_cast<uint16_t>((static_cast<uint16_t>(cmd[0]) << 8) | cmd[1]);
        uint8_t  block = static_cast<uint8_t>((cmd[2] >> 3) & 0x1F);

        // Common Register Block (0x00)
        if (block == 0x00)
        {
            if (addr == 0x002E) return phycfgr;
            return 0x00;
        }

        // Socket 0 Register Block (0x01)
        if (block == 0x01)
        {
            switch (addr)
            {
                case 0x0000: return sn_mr;                 // Sn_MR
                case 0x0001: return 0x00;                  // Sn_CR (command complete)
                case 0x0002: return sn_ir;                 // Sn_IR
                case 0x0003: return sn_sr;                 // Sn_SR
                case 0x001F: return 2;                     // Sn_TXBUF_SIZE (2 KB)
                case 0x0020: return 0x08;                  // Sn_TX_FSR high (2048 free)
                case 0x0021: return 0x00;                  // Sn_TX_FSR low
                case 0x0026: return static_cast<uint8_t>(sn_rx_rsr >> 8);   // Sn_RX_RSR high
                case 0x0027: return static_cast<uint8_t>(sn_rx_rsr & 0xFF); // Sn_RX_RSR low
                default:     return 0x00;
            }
        }

        // Socket 0 RX Buffer Block (0x03)
        if (block == 0x03)
        {
            if (rx_mem_pos < sizeof(rx_mem))
            {
                return rx_mem[rx_mem_pos++];
            }
            return 0x00;
        }

        return 0x00;
    }

    uint8_t socketStatus(uint8_t)
    {
        return 0;
    }

    uint16_t rxAvailable(uint8_t)
    {
        return 0;
    }

    uint16_t txAvailable(uint8_t)
    {
        return 0;
    }

};

static MockW5500Device g_mock_device;
static Ethernet* g_active_driver = nullptr;

static void hook_wizchip_select()
{
    g_mock_device.select();
    if (g_active_driver != nullptr)
    {
        g_active_driver->wizchipSelect();
    }
}

static void hook_wizchip_deselect()
{
    g_mock_device.deselect();
    if (g_active_driver != nullptr)
    {
        g_active_driver->wizchipDeselect();
    }
}

static uint8_t hook_wizchip_read_byte()
{
    // If test explicitly injected raw bytes into mock HAL, use them first
    if (get_spi_rx_buffer_count() > 0)
    {
        return g_active_driver->wizchipReadByte();
    }
    return g_mock_device.read_byte();
}

static void hook_wizchip_write_byte(uint8_t byte)
{
    g_mock_device.write_byte(byte);
    if (g_active_driver != nullptr)
    {
        g_active_driver->wizchipWriteByte(byte);
    }
}

static void hook_wizchip_read_burst(uint8_t* pBuf, uint16_t len)
{
    for (uint16_t i = 0; i < len; ++i)
    {
        pBuf[i] = hook_wizchip_read_byte();
    }
}

static void hook_wizchip_write_burst(uint8_t* pBuf, uint16_t len)
{
    for (uint16_t i = 0; i < len; ++i)
    {
        hook_wizchip_write_byte(pBuf[i]);
    }
}

struct W5500Fixture
{
    EthernetConfig config;
    EthernetTransport transport;
    Ethernet ethernet;

    W5500Fixture() : config(&gpioa), transport(config), ethernet(transport)
    {
        g_active_driver = &ethernet;
        g_mock_device.reset();

        reg_wizchip_cs_cbfunc(hook_wizchip_select, hook_wizchip_deselect);
        reg_wizchip_spi_cbfunc(hook_wizchip_read_byte, hook_wizchip_write_byte);
        reg_wizchip_spiburst_cbfunc(hook_wizchip_read_burst, hook_wizchip_write_burst);

        clear_spi_tx_buffer();
        clear_spi_rx_buffer();
        reset_gpio_port_state(&gpioa);
        init_spi_handle(&hspi1);
    }

    ~W5500Fixture()
    {
        g_active_driver = nullptr;
    }
};

// =============================================================================
// Unit Tests
// =============================================================================

TEST_CASE_FIXTURE(W5500Fixture, "Driver can be constructed")
{
    CHECK(&config != nullptr);
    CHECK(&transport != nullptr);
    CHECK(&ethernet != nullptr);
}

TEST_CASE_FIXTURE(W5500Fixture, "Single byte write forwards to transport")
{
    ethernet.wizchipWriteByte(0x42);

    REQUIRE(get_spi_tx_buffer_count() == 1);
    CHECK(get_spi_tx_buffer()[0] == 0x42);
}

TEST_CASE_FIXTURE(W5500Fixture, "Single byte read forwards to transport")
{
    uint8_t injected = 0xAB;
    inject_spi_rx_data(&injected, sizeof(injected));

    uint8_t value = ethernet.wizchipReadByte();

    CHECK(value == 0xAB);
}

TEST_CASE_FIXTURE(W5500Fixture, "Burst write forwards to transport")
{
    uint8_t tx[] = {0x11, 0x22, 0x33, 0x44};
    ethernet.wizchipWriteBurst(tx, static_cast<uint16_t>(sizeof(tx)));

    REQUIRE(get_spi_tx_buffer_count() == sizeof(tx));
    CHECK(memcmp(get_spi_tx_buffer(), tx, sizeof(tx)) == 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "Burst read forwards to transport")
{
    uint8_t expected[] = {0xAA, 0xBB, 0xCC, 0xDD};
    inject_spi_rx_data(expected, sizeof(expected));

    uint8_t actual[sizeof(expected)]{};
    ethernet.wizchipReadBurst(actual, static_cast<uint16_t>(sizeof(actual)));

    CHECK(memcmp(actual, expected, sizeof(expected)) == 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "Chip select callback controls GPIO pin state")
{
    ethernet.wizchipSelect();
    CHECK(get_gpio_pin_state(&gpioa, GPIO_PIN_5) == GPIO_PIN_RESET);

    ethernet.wizchipDeselect();
    CHECK(get_gpio_pin_state(&gpioa, GPIO_PIN_5) == GPIO_PIN_SET);
}

TEST_CASE_FIXTURE(W5500Fixture, "linkUp returns true when PHY link bit is set")
{
    g_mock_device.phycfgr = 0x01;
    CHECK(ethernet.linkUp() == true);
}

TEST_CASE_FIXTURE(W5500Fixture, "linkUp returns false when PHY link bit is cleared")
{
    g_mock_device.phycfgr = 0x00;
    CHECK(ethernet.linkUp() == false);
}

TEST_CASE_FIXTURE(W5500Fixture, "socketStatus reads status register via SPI")
{
    g_mock_device.sn_sr = 0x22;
    CHECK(ethernet.socketStatus(0) == 0x22);
}

TEST_CASE_FIXTURE(W5500Fixture, "rxAvailable reads 16-bit RX size via SPI")
{
    g_mock_device.sn_rx_rsr = 384;
    CHECK(ethernet.rxAvailable(0) == 384);
}

TEST_CASE_FIXTURE(W5500Fixture, "txAvailable reads 16-bit free TX buffer size via SPI")
{
    CHECK(ethernet.txAvailable(0) == 2048);
}

TEST_CASE_FIXTURE(W5500Fixture, "closeUdp writes CLOSE command and transitions to SOCK_CLOSED")
{
    ethernet.closeUdp(0);
    CHECK(g_mock_device.sn_sr == SOCK_CLOSED);
    CHECK(get_spi_tx_buffer_count() > 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "openUdp successfully opens a UDP socket")
{
    bool success = ethernet.openUdp(0, 5000);

    CHECK(success == true);
    CHECK(g_mock_device.sn_sr == SOCK_UDP);
}

TEST_CASE_FIXTURE(W5500Fixture, "sendUdp transmits UDP payload to destination")
{
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    IPv4Address dest = {{192, 168, 1, 100}};
    uint16_t dest_port = 8080;

    int32_t sent = ethernet.sendUdp(0, payload, static_cast<uint16_t>(sizeof(payload)), dest, dest_port);

    CHECK(sent == static_cast<int32_t>(sizeof(payload)));
    CHECK(get_spi_tx_buffer_count() > 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "receiveUdp parses received datagram, source IP and port")
{
    // Populate mock RX buffer with packet:
    // Header (8 bytes): IP (4B) + Port (2B) + Length (2B)
    g_mock_device.sn_rx_rsr = 12; // 8B header + 4B payload
    g_mock_device.rx_mem[0] = 192;
    g_mock_device.rx_mem[1] = 168;
    g_mock_device.rx_mem[2] = 1;
    g_mock_device.rx_mem[3] = 42;


    
    g_mock_device.rx_mem[4] = 0x23; // Port 9000 (0x2328)
    g_mock_device.rx_mem[5] = 0x28;
    g_mock_device.rx_mem[6] = 0x00; // Payload length 4
    g_mock_device.rx_mem[7] = 0x04;

    // Payload (4 bytes)
    g_mock_device.rx_mem[8]  = 0x01;
    g_mock_device.rx_mem[9]  = 0x02;
    g_mock_device.rx_mem[10] = 0x03;
    g_mock_device.rx_mem[11] = 0x04;

    uint8_t buffer[64] = {};
    IPv4Address source = {};
    uint16_t source_port = 0;

    int32_t received = ethernet.receiveUdp(0, buffer, sizeof(buffer), source, source_port);

    CHECK(received == 4);
    CHECK(source.octet[0] == 192);
    CHECK(source.octet[1] == 168);
    CHECK(source.octet[2] == 1);
    CHECK(source.octet[3] == 42);
    CHECK(source_port == 9000);
    CHECK(buffer[0] == 0x01);
    CHECK(buffer[1] == 0x02);
    CHECK(buffer[2] == 0x03);
    CHECK(buffer[3] == 0x04);
}

TEST_CASE_FIXTURE(W5500Fixture, "setNetworkInfo transmits network configuration registers")
{
    wiz_NetInfo info{};
    info.mac[0] = 0x00; info.mac[1] = 0x08; info.mac[2] = 0xDC;
    info.mac[3] = 0x01; info.mac[4] = 0x02; info.mac[5] = 0x03;
    info.ip[0]  = 192;  info.ip[1]  = 168;  info.ip[2]  = 1;    info.ip[3]  = 50;
    info.sn[0]  = 255;  info.sn[1]  = 255;  info.sn[2]  = 255;  info.sn[3]  = 0;
    info.gw[0]  = 192;  info.gw[1]  = 168;  info.gw[2]  = 1;    info.gw[3]  = 1;
    info.dns[0] = 8;    info.dns[1] = 8;    info.dns[2] = 8;    info.dns[3] = 8;
    info.dhcp = NETINFO_STATIC;

    bool result = ethernet.setNetworkInfo(info);

    CHECK(result == true);
    CHECK(get_spi_tx_buffer_count() > 0);
}