#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "MR25H10.hpp"
#include "RecordRegistry.hpp"
#include "StorageBackend.hpp"

template <typename Transport,
          std::size_t MaxChunkSize = 64,
          std::size_t ChipCapacity = 128 * 1024> // MR25H10 = 1 Mbit = 128 KiB
class MR25H10Backend
{
    static_assert(MaxChunkSize > 0 &&
                  MaxChunkSize <= std::numeric_limits<std::uint16_t>::max(),
                  "MaxChunkSize must be in [1, 65535]");
    static_assert(ChipCapacity <= (1u << 24),
                  "ChipCapacity exceeds MR25H10 24-bit address space");

public:
    explicit MR25H10Backend(MR25H10<Transport>& chip) : chip_(chip) {}

    bool readRecord(const RecordRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!validate_record_range(entry, offset, len))
            return false;
        if (entry.offset > ChipCapacity ||
            offset > ChipCapacity - entry.offset ||
            len > ChipCapacity - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!dst)
            return false;

        const std::size_t base_addr = entry.offset + offset;
        std::size_t done = 0;
        while (done < len)
        {
            const std::uint16_t chunk = static_cast<std::uint16_t>(
                std::min<std::size_t>(len - done, MaxChunkSize));
            const std::uint32_t addr = static_cast<std::uint32_t>(base_addr + done);

            if (!chip_.readBytes(addr, dst + done, chunk))
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
        if (entry.offset > ChipCapacity ||
            offset > ChipCapacity - entry.offset ||
            len > ChipCapacity - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!src)
            return false;

        const std::size_t base_addr = entry.offset + offset;
        std::size_t done = 0;
        while (done < len)
        {
            const std::uint16_t chunk = static_cast<std::uint16_t>(
                std::min<std::size_t>(len - done, MaxChunkSize));
            const std::uint32_t addr = static_cast<std::uint32_t>(base_addr + done);

            if (!chip_.writeBytes(addr, src + done, chunk))
                return false;
            done += chunk;
        }
        return true;
    }

private:
    MR25H10<Transport>& chip_;
};