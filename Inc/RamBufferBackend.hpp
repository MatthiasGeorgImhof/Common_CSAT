#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "RecordRegistry.hpp"
#include "StorageBackend.hpp"
#include "RamStorageMock.hpp"

class RamBufferBackend
{
public:
    RamBufferBackend(std::uint8_t* base, std::size_t total_size)
        : base_(base), total_size_(total_size) {}

    bool readRecord(const RecordRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!base_ || !validate_record_range(entry, offset, len))
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

    bool writeRecord(const RecordRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!base_ || !validate_record_range(entry, offset, len))
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

static_assert(StorageBackend<RamBufferBackend>);

template <std::size_t Size>
class RamMockBackend
{
public:
    explicit RamMockBackend(RamStorageMock<Size>& mock) : mock_(mock) {}

    bool readRecord(const RecordRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!validate_record_range(entry, offset, len))
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

    bool writeRecord(const RecordRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!validate_record_range(entry, offset, len))
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
    RamStorageMock<Size>& mock_;
};