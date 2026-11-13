#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

#include "SX126x.hpp"
#include "SX126xConfigurator.hpp"

class MockRadio
{
public:

    std::vector<std::string> calls;

    sx126x::PacketType packet_type{};
    uint32_t frequency{};
    sx126x::PaConfig pa{};
    int8_t power{};
    sx126x::LoRaModulationParams modulation{};
    sx126x::LoRaPacketParams packet{};
    sx126x::IrqMasks irq{};

    bool Init()
    {
        calls.push_back("Init");
        return true;
    }

    void ClearDeviceErrors()
    {
        calls.push_back("ClearDeviceErrors");
    }

    void SetDio3AsTcxoCtrl(uint8_t /*voltage*/, uint32_t /*timeout*/)
    {
        calls.push_back("SetDio3AsTcxoCtrl");
    }

    void SetDio2AsRfSwitchCtrl(bool /*value*/)
    {
        calls.push_back("SetDio2AsRfSwitchCtrl");
    }

    void SetPacketType(sx126x::PacketType v)
    {
        packet_type = v;
        calls.push_back("SetPacketType");
    }

    void SetFrequency(uint32_t v)
    {
        frequency = v;
        calls.push_back("SetFrequency");
    }

    void SetPaConfig(sx126x::PaConfig v)
    {
        pa = v;
        calls.push_back("SetPaConfig");
    }

    void SetTxPower(int8_t v)
    {
        power = v;
        calls.push_back("SetTxPower");
    }

    void SetBufferBaseAddress(uint8_t /*addr*/, uint8_t /*offset*/)
    {
        calls.push_back("SetBufferBaseAddress");
    }

    void SetModulationParams(
        sx126x::LoRaModulationParams v)
    {
        modulation = v;
        calls.push_back("SetModulationParams");
    }

    void SetPacketParams(
        sx126x::LoRaPacketParams v)
    {
        packet = v;
        calls.push_back("SetPacketParams");
    }

    void SetDioIrqParams(
        sx126x::IrqMasks v)
    {
        irq = v;
        calls.push_back("SetDioIrqParams");
    }
};

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

TEST_CASE("SX126xConfigurator configures radio")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();

    SX126xConfigurator< MockRadio> uut(radio, cfg);

    CHECK(uut.configure());

    CHECK(radio.frequency == 916500000);
    CHECK(radio.power == 14);
    CHECK(radio.packet_type == sx126x::PacketType::LoRa);
    CHECK(radio.modulation.sf == 7);
    CHECK(radio.packet.payload_length == 15);
}

TEST_CASE("SX126xConfigurator enables TCXO")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();

    SX126xConfigurator< MockRadio> uut(radio, cfg);

    uut.configure();

    CHECK(std::find(radio.calls.begin(), radio.calls.end(), "SetDio3AsTcxoCtrl") != radio.calls.end());
}

TEST_CASE("SX126xConfigurator skips TCXO when disabled")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();

    cfg.tcxo_enable = false;

    SX126xConfigurator< MockRadio> uut(radio, cfg);

    uut.configure();

    CHECK(std::find(radio.calls.begin(), radio.calls.end(), "SetDio3AsTcxoCtrl") == radio.calls.end());
}

TEST_CASE("SX126xConfigurator call sequence")
{
    MockRadio radio;
    SX126xRadioConfig cfg = create_config();

    SX126xConfigurator< MockRadio> uut(radio, cfg);

    uut.configure();

    REQUIRE(radio.calls.size() == 12);
    CHECK(radio.calls[0] == "Init");
    CHECK(radio.calls[1] == "SetDio3AsTcxoCtrl");
    CHECK(radio.calls[2] == "ClearDeviceErrors");
    CHECK(radio.calls[3] == "SetDio2AsRfSwitchCtrl");
    CHECK(radio.calls[4] == "SetPacketType");
    CHECK(radio.calls[5] == "SetFrequency");
    CHECK(radio.calls[6] == "SetPaConfig");
    CHECK(radio.calls[7] == "SetTxPower");
    CHECK(radio.calls[8] == "SetBufferBaseAddress");
    CHECK(radio.calls[9] == "SetModulationParams");
    CHECK(radio.calls[10] == "SetPacketParams");
    CHECK(radio.calls[11] == "SetDioIrqParams");
}
