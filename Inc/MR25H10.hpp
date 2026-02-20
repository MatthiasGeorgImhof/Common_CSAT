#ifndef INC_MR25H10_H_
#define INC_MR25H10_H_

#include <cstdint>
#include <optional>
#include "Transport.hpp"

#ifdef __x86_64__
#include "mock_hal.h"
#endif

enum class MR25H10_COMMANDS : uint8_t
{
    MR25H10_WREN = 0x06,
    MR25H10_WRDI = 0x04,
    MR25H10_RDSR = 0x05,
    MR25H10_WRSR = 0x01,
    MR25H10_READ = 0x03,
    MR25H10_WRITE = 0x02,
    MR25H10_SLEEP = 0xB9,
    MR25H10_WAKE = 0xAB,
};

template <typename Transport>
    requires StreamModeTransport<Transport>
class MR25H10
{
public:
    MR25H10() = delete;
    explicit MR25H10(const Transport& transport) : transport_(transport) {}

    std::optional<uint8_t> readStatus() const;
    bool writeStatus(uint8_t status) const;

    bool readBytes(uint32_t addr, void* buf, uint16_t len) const;
    bool writeBytes(uint32_t addr, const void* buf, uint16_t len) const;

private:
    const Transport& transport_;
};

template <typename Transport>
    requires StreamModeTransport<Transport>
std::optional<uint8_t> MR25H10<Transport>::readStatus() const
{
    uint8_t tx[1] = { static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_RDSR) };
    uint8_t rx[1] = { 0 };

    if (!transport_.transfer(tx, rx, 1))
        return std::nullopt;

    return rx[0];
}

template <typename Transport>
    requires StreamModeTransport<Transport>
bool MR25H10<Transport>::writeStatus(uint8_t status) const
{
    uint8_t tx_buf[2] = {
        static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_WRSR),
        status
    };
    return transport_.write(tx_buf, sizeof(tx_buf));
}

template <typename Transport>
    requires StreamModeTransport<Transport>
bool MR25H10<Transport>::readBytes(uint32_t addr, void* buf, uint16_t len) const
{
    uint8_t header[4] = {
        static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_READ),
        static_cast<uint8_t>((addr >> 16) & 0xFF),
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF)
    };

    // Send command + address
    if (!transport_.write(header, sizeof(header)))
        return false;

    // Read payload
    return transport_.read(static_cast<uint8_t*>(buf), len);
}

template <typename Transport>
    requires StreamModeTransport<Transport>
bool MR25H10<Transport>::writeBytes(uint32_t addr, const void* buf, uint16_t len) const
{
    // Enable writes
    uint8_t wren = static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_WREN);
    if (!transport_.write(&wren, 1))
        return false;

    uint8_t header[4] = {
        static_cast<uint8_t>(MR25H10_COMMANDS::MR25H10_WRITE),
        static_cast<uint8_t>((addr >> 16) & 0xFF),
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF)
    };

    // Send header
    if (!transport_.write(header, sizeof(header)))
        return false;

    // Send payload
    return transport_.write(static_cast<const uint8_t*>(buf), len);
}

#endif /* INC_MR25H10_H_ */
