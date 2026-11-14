#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "SX126x.hpp"
#include "SX126xConfigurator.hpp"
#include "SX126xController.hpp"

#include "CircularBuffer.hpp"

struct Data {
    int a;
    float b;
};

using MockRxBuffer = CircularBuffer<Data, 32>;

SX126xRadioConfig create_config()
{
    return SX126xRadioConfig {
        .packet_type  = sx126x::PacketType::LoRa,
        .frequency_hz = 916500000,

        .pa_config = {
            0x04,
            0x07,
            0x00,
            0x01
        },

        .power_dbm = 14,

        .modulation = {
            7,
            4,
            1,
            0
        },

        .packet = {
            8,
            sx126x::HeaderType::Variable,
            15,
            1,
            0
        },

        .irqs = {
            sx126x::IRQ::TxDone,
            sx126x::IRQ::RxDone,
            sx126x::IRQ::None,
            sx126x::IRQ::None
        },

        .rf_switch_enable = true,

        .tcxo_enable = true,
        .tcxo_voltage = 7,
        .tcxo_timeout_ticks = 320
    };
}

class MockRadio
{
public:

    std::vector<std::string> calls;

    sx126x::IRQ irq_status = sx126x::IRQ::None;

    sx126x::LoRaPacketParams last_packet_params{};

    sx126x::RxBufferStatus rx_info
    {
        .payload_length = 0,
        .buffer_pointer = 0
    };

    std::array<uint8_t, 256> rx_data{};

    void SetPacketParams(const sx126x::LoRaPacketParams p)
    {
        last_packet_params = p;
        calls.push_back("SetPacketParams");
    }

    void WriteBuffer(
        uint8_t offset,
        const uint8_t* data,
        uint8_t len)
    {
        (void)offset;
        (void)data;
        (void)len;

        calls.push_back("WriteBuffer");
    }

    void StartTx()
    {
        calls.push_back("StartTx");
    }

    void StartRx()
    {
        calls.push_back("StartRx");
    }

    sx126x::IRQ GetIrqStatus()
    {
        calls.push_back("GetIrqStatus");
        return irq_status;
    }

    void ClearIrqStatus(sx126x::IRQ irq)
    {
        (void)irq;
        calls.push_back("ClearIrqStatus");
    }

    sx126x::RxBufferStatus GetRxBufferStatus()
    {
        calls.push_back("GetRxBufferStatus");
        return rx_info;
    }

    void ReadBuffer(
        uint8_t offset,
        uint8_t* dst,
        uint8_t len)
    {
        (void)offset;

        calls.push_back("ReadBuffer");

        std::memcpy(dst, rx_data.data(), len);
    }

    void SetStandby()
    {
        calls.push_back("SetStandby");
    }
};

TEST_CASE("controller init")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    CHECK(uut.busy());
    CHECK(radio.calls.empty());
}

TEST_CASE("transmit rejected while controller not RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    uint8_t payload[10]{};

    CHECK(uut.busy());
    REQUIRE_FALSE( uut.transmit(payload, 10));
    CHECK(radio.calls.empty());
}

TEST_CASE("notify_irq performs no radio call")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    uut.notify_irq();

    CHECK(radio.calls.empty());
}

TEST_CASE("process without irq does nothing")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    uut.process();

    CHECK(radio.calls.empty());
}

TEST_CASE("tx done enters RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    radio.irq_status = sx126x::IRQ::TxDone;

    uut.notify_irq();
    uut.process();

    REQUIRE(radio.calls.size() >= 4);
    CHECK(radio.calls[0] == "GetIrqStatus");
    CHECK(radio.calls[1] == "ClearIrqStatus");
    CHECK( std::find( radio.calls.begin(), radio.calls.end(), "SetPacketParams") != radio.calls.end());
    CHECK( std::find( radio.calls.begin(), radio.calls.end(), "StartRx") != radio.calls.end());
    CHECK( radio.last_packet_params.payload_length == sx126x::MTU);
}

TEST_CASE("crc error reenters RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    radio.irq_status = sx126x::IRQ::CrcErr;

    uut.notify_irq();
    uut.process();

    CHECK( std::find( radio.calls.begin(), radio.calls.end(), "StartRx") != radio.calls.end()); 
}

TEST_CASE("timeout reenters RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    radio.irq_status = sx126x::IRQ::Timeout;

    uut.notify_irq();
    uut.process();

    CHECK( std::find( radio.calls.begin(), radio.calls.end(), "StartRx") != radio.calls.end());
}

TEST_CASE("RX_DONE commits data into rx buffer")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    Data expected
    {
        .a = 1234,
        .b = 5.5f
    };

    static_assert(sizeof(Data) <= 255);

    std::memcpy( radio.rx_data.data(), &expected, sizeof(Data));
    radio.rx_info.payload_length = static_cast<uint8_t>(sizeof(Data));
    radio.rx_info.buffer_pointer = 0;
    radio.irq_status = sx126x::IRQ::RxDone;

    uut.notify_irq();
    uut.process();

    REQUIRE(rx.size() == 1);

    Data actual = rx.pop();

    CHECK(actual.a == expected.a);
    CHECK(actual.b == doctest::Approx(expected.b));
    CHECK(radio.calls[0] == "GetIrqStatus");
    CHECK(radio.calls[1] == "ClearIrqStatus");
    CHECK(radio.calls[2] == "GetRxBufferStatus");
    CHECK(radio.calls[3] == "ReadBuffer");
}

TEST_CASE("TX_DONE restores receive MTU")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    radio.irq_status = sx126x::IRQ::TxDone;

    uut.notify_irq();
    uut.process();

    CHECK( radio.last_packet_params.payload_length == sx126x::MTU);
}

TEST_CASE("RX_DONE does not restart RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    radio.rx_info.payload_length = static_cast<uint8_t>(sizeof(Data));

    radio.irq_status = sx126x::IRQ::RxDone;

    uut.notify_irq();
    uut.process();

    auto count = std::count( radio.calls.begin(), radio.calls.end(), "StartRx");

    CHECK(count == 0);
}

TEST_CASE("combined IRQ flags are processed")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer > uut(radio, cfg, rx);

    radio.rx_info.payload_length = static_cast<uint8_t>(sizeof(Data));

    radio.irq_status = sx126x::IRQ::RxDone | sx126x::IRQ::CrcErr;

    uut.notify_irq();
    uut.process();

    CHECK( std::find( radio.calls.begin(), radio.calls.end(), "ReadBuffer") != radio.calls.end());
    CHECK( std::find( radio.calls.begin(), radio.calls.end(), "StartRx") != radio.calls.end());
}

TEST_CASE("controller starts halted")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    using Controller = SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer >;
    Controller uut(radio, cfg, rx);

    CHECK( uut.state() == Controller::SX126xState::HALT);

    CHECK( uut.busy());

    CHECK( radio.calls.empty());
}

TEST_CASE("start enters RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    using Controller = SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer >;
    Controller uut(radio, cfg, rx);

    uut.start();

    CHECK( uut.state() == Controller::SX126xState::RX);
    CHECK_FALSE( uut.busy());
    REQUIRE( radio.calls.size() == 2);
    CHECK( radio.calls[0] == "SetPacketParams");
    CHECK( radio.calls[1] == "StartRx");
    CHECK( radio.last_packet_params.payload_length == sx126x::MTU);
}

TEST_CASE("stop enters HALT")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    using Controller = SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer >;
    Controller uut(radio, cfg, rx);

    uut.start();

    radio.calls.clear();

    uut.stop();

    CHECK( uut.state() == Controller::SX126xState::HALT);
    CHECK( uut.busy());
    REQUIRE( radio.calls.size() == 1);
    CHECK( radio.calls[0] == "SetStandby");
}

TEST_CASE("transmit accepted in RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    using Controller = SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer >;
    Controller uut(radio, cfg, rx);

    uut.start();

    radio.calls.clear();

    uint8_t payload[10]{};

    REQUIRE( uut.transmit(payload, 10));
    CHECK( uut.state() == Controller::SX126xState::TX_ACTIVE);
    REQUIRE( radio.calls.size() == 3);
    CHECK( radio.calls[0] == "SetPacketParams");
    CHECK( radio.calls[1] == "WriteBuffer");
    CHECK( radio.calls[2] == "StartTx");
}

TEST_CASE("transmit rejected while TX active")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    using Controller = SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer >;
    Controller uut(radio, cfg, rx);

    uut.start();

    uint8_t payload[10]{};

    REQUIRE(uut.transmit(payload, 10));
    REQUIRE_FALSE(uut.transmit(payload, 10));
}

TEST_CASE("TX_DONE returns controller to RX")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();
    MockRxBuffer rx;

    using Controller = SX126xController< MockRadio, SX126xRadioConfig, MockRxBuffer >;
    Controller uut(radio, cfg, rx);
    uut.start();

    uint8_t payload[10]{};

    REQUIRE( uut.transmit(payload, 10));
    CHECK( uut.state() == Controller::SX126xState::TX_ACTIVE);

    radio.calls.clear();

    radio.irq_status = sx126x::IRQ::TxDone;

    uut.notify_irq();
    uut.process();

    CHECK( uut.state() == Controller::SX126xState::RX);
    CHECK_FALSE( uut.busy());
}
