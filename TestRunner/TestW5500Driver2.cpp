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

void print(const char*) {
    // Do nothing for tests
}

extern "C"
{
    void DNS_init(uint8_t, uint8_t*)
    {
    }
}

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

template class W5500Driver<EthernetTransport, EthernetResetPin, NETWORK>;

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

    // RX buffer memory for Socket data
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

    void select()   { cmd_count = 0; }
    void deselect() { cmd_count = 0; }

    void write_byte(uint8_t b)
    {
        if (cmd_count < 3)
        {
            cmd[cmd_count++] = b;
        }
        else
        {
            uint16_t addr  = static_cast<uint16_t>((static_cast<uint16_t>(cmd[0]) << 8) | cmd[1]);
            uint8_t  block = static_cast<uint8_t>((cmd[2] >> 3) & 0x1F);

            // Any socket register block: (block & 0x03) == 0x01
            if ((block & 0x03) == 0x01)
            {
                if (addr == 0x0000) // Sn_MR
                {
                    sn_mr = b;
                }
                else if (addr == 0x0001) // Sn_CR
                {
                    if (b == Sn_CR_OPEN)
                    {
                        sn_sr = ((sn_mr & 0x0F) == Sn_MR_TCP) ? SOCK_INIT : SOCK_UDP;
                    }
                    else if (b == Sn_CR_LISTEN)
                    {
                        sn_sr = SOCK_LISTEN;
                    }
                    else if (b == Sn_CR_CONNECT)
                    {
                        sn_sr = SOCK_ESTABLISHED;
                    }
                    else if (b == Sn_CR_CLOSE || b == Sn_CR_DISCON)
                    {
                        sn_sr = SOCK_CLOSED;
                    }
                }
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
            if (addr == 0x002E) return phycfgr; // PHYCFGR (Link status)
            if (addr == 0x0039) return 0x04;     // VERSIONR (W5500 version is 0x04)

            // Source IP (SIPR) - non-zero so TCP sockets can open
            if (addr == 0x000F) return 192;
            if (addr == 0x0010) return 168;
            if (addr == 0x0011) return 1;
            if (addr == 0x0012) return 50;

            return 0x00;
        }

        // Any Socket Register Block: Sockets 0..7 map to blocks where (block & 0x03) == 0x01
        if ((block & 0x03) == 0x01)
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
                case 0x0026: return static_cast<uint8_t>(sn_rx_rsr >> 8);
                case 0x0027: return static_cast<uint8_t>(sn_rx_rsr & 0xFF);
                default:     return 0x00;
            }
        }

        // Socket RX Buffer Block ((block & 0x03) == 0x03)
        if ((block & 0x03) == 0x03)
        {
            if (rx_mem_pos < sizeof(rx_mem))
            {
                return rx_mem[rx_mem_pos++];
            }
            return 0x00;
        }

        return 0x00;
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

    W5500Fixture() : transport(config), ethernet(transport)
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
    CHECK(get_gpio_pin_state(&gpioa, CS_PIN) == GPIO_PIN_RESET);

    ethernet.wizchipDeselect();
    CHECK(get_gpio_pin_state(&gpioa, CS_PIN) == GPIO_PIN_SET);
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
    CHECK(ethernet.socketStatus(Ethernet::SOCKET_0) == 0x22);
}

TEST_CASE_FIXTURE(W5500Fixture, "rxAvailable reads 16-bit RX size via SPI")
{
    g_mock_device.sn_rx_rsr = 384;
    CHECK(ethernet.rxAvailable(Ethernet::SOCKET_0) == 384);
}

TEST_CASE_FIXTURE(W5500Fixture, "txAvailable reads 16-bit free TX buffer size via SPI")
{
    CHECK(ethernet.txAvailable(Ethernet::SOCKET_0) == 2048);
}

TEST_CASE_FIXTURE(W5500Fixture, "closeUdp writes CLOSE command and transitions to SOCK_CLOSED")
{
    ethernet.closeUdp(Ethernet::SOCKET_0);
    CHECK(g_mock_device.sn_sr == SOCK_CLOSED);
    CHECK(get_spi_tx_buffer_count() > 0);
}

TEST_CASE_FIXTURE(W5500Fixture, "openUdp successfully opens a UDP socket")
{
    bool success = ethernet.openUdp(Ethernet::SOCKET_0, 5000);

    CHECK(success == true);
    CHECK(g_mock_device.sn_sr == SOCK_UDP);
}

TEST_CASE_FIXTURE(W5500Fixture, "sendUdp transmits UDP payload to destination")
{
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    IPv4Address dest = {{192, 168, 1, 100}};
    uint16_t dest_port = 8080;

    int32_t sent = ethernet.sendUdp(Ethernet::SOCKET_0, payload, static_cast<uint16_t>(sizeof(payload)), dest, dest_port);

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

    int32_t received = ethernet.receiveUdp(Ethernet::SOCKET_0, buffer, sizeof(buffer), source, source_port);

    CHECK(received == 4);
    CHECK(source.bytes[0] == 192);
    CHECK(source.bytes[1] == 168);
    CHECK(source.bytes[2] == 1);
    CHECK(source.bytes[3] == 42);
    CHECK(source_port == 9000);
    CHECK(buffer[0] == 0x01);
    CHECK(buffer[1] == 0x02);
    CHECK(buffer[2] == 0x03);
    CHECK(buffer[3] == 0x04);
}

TEST_CASE_FIXTURE(W5500Fixture, "W5500Driver initialize compiles and registers callbacks")
{
    uint8_t data[] = {0x04}; // W5500 version
    inject_spi_rx_data(data, sizeof(data));
    auto result = ethernet.initialize();
    CHECK(result == true);
}

// TEST_CASE_FIXTURE(W5500Fixture, "W5500Driver TCP client lifecycle")
// {
//     // 1. Open client socket (Socket 1)
//     CHECK(ethernet.openTcp(Ethernet::SOCKET_1, 50000) == true);
//     CHECK(g_mock_device.sn_sr == SOCK_INIT);

//     // 2. Connect to remote server
//     IPv4Address server_ip = {{192, 168, 1, 1}};
//     CHECK(ethernet.connectTcp(Ethernet::SOCKET_1, server_ip, 8080) == true);
//     CHECK(g_mock_device.sn_sr == SOCK_ESTABLISHED);

//     // 3. Send and Receive in ESTABLISHED state
//     const uint8_t tx_data[] = {0x01, 0x02};
//     CHECK(ethernet.sendTcp(Ethernet::SOCKET_1, tx_data, sizeof(tx_data)) >= 0);

//     uint8_t rx_data[16] = {};
//     CHECK(ethernet.receiveTcp(Ethernet::SOCKET_1, rx_data, sizeof(rx_data)) >= 0);

//     // 4. Disconnect
//     CHECK(ethernet.disconnectTcp(Ethernet::SOCKET_1) == true);
//     CHECK(g_mock_device.sn_sr == SOCK_CLOSED);
// }

// TEST_CASE_FIXTURE(W5500Fixture, "W5500Driver TCP server listen")
// {
//     // Test server passive listening on a dedicated socket (Socket 2)
//     CHECK(ethernet.openTcp(Ethernet::SOCKET_2, 80) == true);
//     CHECK(g_mock_device.sn_sr == SOCK_INIT);

//     CHECK(ethernet.listenTcp(Ethernet::SOCKET_2) == true);
//     CHECK(g_mock_device.sn_sr == SOCK_LISTEN);

//     ethernet.closeTcp(Ethernet::SOCKET_2);
//     CHECK(g_mock_device.sn_sr == SOCK_CLOSED);
// }