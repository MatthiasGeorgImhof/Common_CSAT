#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "Mram.hpp"
#include "MramStorageBackend.hpp"
#include "RamMramMock.hpp"

class RamBufferBackend
{
public:
    RamBufferBackend(std::uint8_t* base, std::size_t total_size)
        : base_(base), total_size_(total_size) {}

    bool readRecord(const MramRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!base_ || !mram_validate_range(entry, offset, len))
            return false;
        if (entry.offset > total_size_ ||
            offset > total_size_ - entry.offset ||
            len > total_size_ - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!dst)
            return false;

        std::memcpy(dst, base_ + entry.offset + offset, len);
        return true;
    }

    bool writeRecord(const MramRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!base_ || !mram_validate_range(entry, offset, len))
            return false;
        if (entry.offset > total_size_ ||
            offset > total_size_ - entry.offset ||
            len > total_size_ - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!src)
            return false;

        std::memcpy(base_ + entry.offset + offset, src, len);
        return true;
    }

private:
    std::uint8_t* base_;
    std::size_t   total_size_;
};

static_assert(MramStorageBackend<RamBufferBackend>);

template <std::size_t Size>
class RamMockBackend
{
public:
    explicit RamMockBackend(RamMramMock<Size>& mock) : mock_(mock) {}

    bool readRecord(const MramRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!mram_validate_range(entry, offset, len))
            return false;
        if (entry.offset > Size ||
            offset > Size - entry.offset ||
            len > Size - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!dst)
            return false;

        std::size_t done = 0;
        while (done < len)
        {
            const std::uint16_t chunk = static_cast<std::uint16_t>(
                std::min<std::size_t>(len - done, std::numeric_limits<std::uint16_t>::max()));
            const std::uint32_t addr = static_cast<std::uint32_t>(entry.offset + offset + done);

            if (!mock_.readBytes(addr, dst + done, chunk))
                return false;
            done += chunk;
        }
        return true;
    }

    bool writeRecord(const MramRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!mram_validate_range(entry, offset, len))
            return false;
        if (entry.offset > Size ||
            offset > Size - entry.offset ||
            len > Size - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!src)
            return false;

        std::size_t done = 0;
        while (done < len)
        {
            const std::uint16_t chunk = static_cast<std::uint16_t>(
                std::min<std::size_t>(len - done, std::numeric_limits<std::uint16_t>::max()));
            const std::uint32_t addr = static_cast<std::uint32_t>(entry.offset + offset + done);

            if (!mock_.writeBytes(addr, src + done, chunk))
                return false;
            done += chunk;
        }
        return true;
    }

private:
    RamMramMock<Size>& mock_;
};