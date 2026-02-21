#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>

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
        storage_.fill(0xFF);   // MRAM defaults to erased state
    }

    // -------------------------------------------------------------------------
    // Read bytes from mock MRAM
    // -------------------------------------------------------------------------
    bool readBytes(uint32_t addr, void* buf, uint16_t len) const
    {
        if (addr + len > kSize)
            return false;

        std::memcpy(buf, storage_.data() + addr, len);
        return true;
    }

    // -------------------------------------------------------------------------
    // Write bytes to mock MRAM
    // -------------------------------------------------------------------------
    bool writeBytes(uint32_t addr, const void* buf, uint16_t len)
    {
        if (addr + len > kSize)
            return false;

        std::memcpy(storage_.data() + addr, buf, len);
        return true;
    }

    // -------------------------------------------------------------------------
    // Reset mock MRAM to erased state
    // -------------------------------------------------------------------------
    void reset(uint8_t fill = 0xFF)
    {
        storage_.fill(fill);
    }

    // -------------------------------------------------------------------------
    // Direct access for tests (optional)
    // -------------------------------------------------------------------------
    const uint8_t* data() const { return storage_.data(); }
    uint8_t*       data()       { return storage_.data(); }

private:
    std::array<uint8_t, Size> storage_;
};
