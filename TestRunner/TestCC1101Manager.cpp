#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "CC1101Manager.hpp"
#include "CircularBuffer.hpp"
#include "mock_hal.h"

// -----------------------------------------------------------------------------
// Mock RADIO implementation
// -----------------------------------------------------------------------------

struct MockRadio
{
    enum class StrobeCommand : uint8_t {
        SIDLE = 0x36,
        SFRX  = 0x3A,
        SRX   = 0x34,
        SFTX  = 0x3B,
        STX   = 0x35
    };

    enum class StatusRegister : uint8_t {
        MARCSTATE = 0x35,
        RXBYTES   = 0x3B
    };

    enum class BurstRegister : uint8_t {
        RXFIFO = 0x3F,
        TXFIFO = 0x3F
    };

    // --- Mock state ---
    uint8_t marcstate = 0x01; // IDLE
    uint8_t rxbytes   = 0;
    uint8_t rx_fifo[64]{};
    uint8_t tx_fifo[64]{};
    uint8_t tx_len = 0;

    bool init_called = false;
    bool strobe_sidle = false;
    bool strobe_sfrx  = false;
    bool strobe_srx   = false;
    bool strobe_sftx  = false;
    bool strobe_stx   = false;

    void Init() { init_called = true; }

    void Strobe(StrobeCommand cmd)
    {
        switch (cmd) {
        case StrobeCommand::SIDLE: strobe_sidle = true; break;
        case StrobeCommand::SFRX:  strobe_sfrx  = true; break;
        case StrobeCommand::SRX:   strobe_srx   = true; break;
        case StrobeCommand::SFTX:  strobe_sftx  = true; break;
        case StrobeCommand::STX:   strobe_stx   = true; break;
        }
    }

    uint8_t ReadStatus(StatusRegister reg)
    {
        if (reg == StatusRegister::MARCSTATE)
            return marcstate;
        if (reg == StatusRegister::RXBYTES)
            return rxbytes;
        return 0;
    }

    void ReadBurst(BurstRegister, uint8_t* dst, uint8_t len)
    {
        memcpy(dst, rx_fifo, len);
    }

    void WriteBurst(BurstRegister, const uint8_t* src, uint8_t len)
    {
        memcpy(tx_fifo, src, len);
        tx_len = len;
    }
};

struct MockGD2Pin {
    bool level = false;
    bool read() const { return level; }
};

// -----------------------------------------------------------------------------
// Test Rings
// -----------------------------------------------------------------------------

using TXRing = CircularBuffer<CC1101Packet, 8>;
using RXRing = CircularBuffer<CC1101Packet, 8>;

using Manager = CC1101Manager<MockRadio, TXRing, RXRing, MockGD2Pin>;

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

TEST_CASE("Manager initializes radio and enters RX_LISTENING")
{
    MockGD2Pin gd2;
    MockRadio radio;
    Manager mgr(radio, gd2);

    mgr.process(); // INIT

    CHECK(radio.init_called);
    CHECK(radio.strobe_sidle);
    CHECK(radio.strobe_sfrx);
    CHECK(radio.strobe_srx);
}

TEST_CASE("TX: pushing a packet results in radio TXFIFO write")
{
    MockGD2Pin gd2;
    MockRadio radio;
    Manager mgr(radio, gd2);

    mgr.process(); // INIT
    mgr.process(); // RX_LISTENING

    // Fake GD2 low so TX is allowed
    gd2.level = false;

    CC1101Packet pkt{};
    pkt[0] = 3;
    pkt[1] = 'A';
    pkt[2] = 'B';
    pkt[3] = 'C';

    CHECK(mgr.push_tx_packet(pkt));

    mgr.process(); // RX_LISTENING → TX_START
    mgr.process(); // TX_START → TX_WAIT_END

    CHECK(radio.strobe_sftx);
    CHECK(radio.strobe_stx);
    CHECK(radio.tx_len == 4);
    CHECK(radio.tx_fifo[1] == 'A');
}

TEST_CASE("RX: valid packet is committed into RX ring")
{
    MockRadio radio;
    MockGD2Pin gd2;
    Manager mgr(radio, gd2);

    mgr.process(); // INIT
    mgr.process(); // RX_LISTENING

    // Prepare a valid CC1101 packet in radio FIFO
    radio.rxbytes = 5; // len=2 + payload(2) + RSSI + LQI
    radio.rx_fifo[0] = 2;
    radio.rx_fifo[1] = 'X';
    radio.rx_fifo[2] = 'Y';
    radio.rx_fifo[3] = 0x55;      // RSSI
    radio.rx_fifo[4] = 0x80 | 42; // LQI + CRC_OK

    radio.marcstate = 0x01; // IDLE

    mgr.notify_packet_received();
    mgr.process(); // RX_LISTENING → RX_PROCESSING
    mgr.process(); // RX_PROCESSING → force_rx()

    CC1101Packet out{};
    CHECK(mgr.pop_rx_packet(out));

    CHECK(out[0] == 2);
    CHECK(out[1] == 'X');
    CHECK(out[2] == 'Y');
    CHECK(out[3] == 0x55);
    CHECK((out[4] & 0x80) != 0);
}

TEST_CASE("RX: invalid CRC packet is discarded")
{
    MockRadio radio;
    MockGD2Pin gd2;
    Manager mgr(radio, gd2);

    mgr.process(); // INIT
    mgr.process(); // RX_LISTENING

    radio.rxbytes = 5;
    radio.rx_fifo[0] = 2;
    radio.rx_fifo[1] = 'X';
    radio.rx_fifo[2] = 'Y';
    radio.rx_fifo[3] = 0x55;
    radio.rx_fifo[4] = 0x00; // CRC not OK

    mgr.notify_packet_received();
    mgr.process(); // RX_LISTENING → RX_PROCESSING
    mgr.process(); // RX_PROCESSING

    CC1101Packet out{};
    CHECK(!mgr.pop_rx_packet(out)); // nothing committed
}
