#pragma once

#include <cstdint>

constexpr uint8_t SMALL_DATA_MTU = 8;   // classical CAN
constexpr uint8_t MEDIUM_DATA_MTU = 59; // packet Radio, 4 bytes extheader + length byte <= 64
constexpr uint8_t LARGE_DATA_MTU = 249; // serial and UDP, 4 bytes extheader <= 253 