#pragma once

#include <cstdint>
#include "SX126x.hpp"

struct SX126xRadioConfig
{
    sx126x::PacketType packet_type;
    uint32_t frequency_hz;
    sx126x::PaConfig pa_config;
    int8_t power_dbm;
    sx126x::LoRaModulationParams modulation;
    sx126x::LoRaPacketParams packet;
    sx126x::IrqMasks irqs;
    bool rf_switch_enable;
    bool tcxo_enable;
    uint8_t tcxo_voltage;
    uint32_t tcxo_timeout_ticks;
};

template< typename Radio>
class SX126xConfigurator
{
public:

    SX126xConfigurator( Radio& radio, SX126xRadioConfig& config) : radio_(radio), config_(config) {}

    bool configure()
    {
        radio_.Init();

        if (config_.tcxo_enable)
        {
            radio_.SetDio3AsTcxoCtrl( config_.tcxo_voltage, config_.tcxo_timeout_ticks);
        }

        radio_.ClearDeviceErrors();
        radio_.SetDio2AsRfSwitchCtrl(config_.rf_switch_enable);
        radio_.SetPacketType(config_.packet_type);
        radio_.SetFrequency(config_.frequency_hz);
        radio_.SetPaConfig(config_.pa_config);
        radio_.SetTxPower(config_.power_dbm);
        radio_.SetBufferBaseAddress(0x00, 0x80);
        radio_.SetModulationParams(config_.modulation);
        radio_.SetPacketParams(config_.packet);
        radio_.SetDioIrqParams(config_.irqs);

        return true;
    }

private:

    Radio& radio_;
    SX126xRadioConfig& config_;
};
