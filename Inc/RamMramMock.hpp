#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// -----------------------------------------------------------------------------
// RamMramMock: a deterministic, byte-addressable MRAM mock for testing
// -----------------------------------------------------------------------------
template <std::size_t Size>
class RamMramMock
{
public:
    static constexpr std::size_t kSize = Size;

    RamMramMock()
    {
        storage_.fill(0xFF); // MRAM defaults to erased state
    }

    bool readBytes(std::uint32_t addr, void* buf, std::uint16_t len) const
    {
        if (addr > kSize || len > kSize - addr)
            return false;
        if (len == 0)
            return true;
        if (!buf)
            return false;

        std::memcpy(buf, storage_.data() + addr, len);
        return true;
    }

    bool writeBytes(std::uint32_t addr, const void* buf, std::uint16_t len)
    {
        if (addr > kSize || len > kSize - addr)
            return false;
        if (len == 0)
            return true;
        if (!buf)
            return false;

        std::memcpy(storage_.data() + addr, buf, len);
        return true;
    }

    void reset(std::uint8_t fill = 0xFF)
    {
        storage_.fill(fill);
    }

    const std::uint8_t* data() const { return storage_.data(); }
    std::uint8_t*       data()       { return storage_.data(); }

private:
    std::array<std::uint8_t, Size> storage_;
};