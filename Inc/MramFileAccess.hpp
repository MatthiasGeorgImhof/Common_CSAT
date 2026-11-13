#ifndef __MRAM_FILE_ACCESS_H__
#define __MRAM_FILE_ACCESS_H__

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "FileAccess.hpp"
#include "Mram.hpp"
#include "MramStorageBackend.hpp"
#include "MramStore.hpp"

enum class MramFileMode
{
    PayloadOnly, // Reads validated payload T (requires matching version & CRC)
    RawRecord    // Reads raw MramRecord<T> bytes (header + payload)
};

template <typename Layout, MramStorageBackend Backend>
class MramFileAccess
{
public:
    explicit MramFileAccess(const Backend& backend,
                            MramFileMode mode = MramFileMode::RawRecord)
        : backend_(backend), mode_(mode) {}

    bool read(const FileName& path_array,
              std::size_t offset,
              std::uint8_t* buffer,
              std::size_t& size) const
    {
        const auto it = std::find(path_array.begin(), path_array.end(), '\0');
        const std::string_view path(path_array.data(),
                                    static_cast<std::size_t>(it - path_array.begin()));

        const auto* entry = MramRegistry<Layout>::lookup(path);
        if (!entry)
        {
            size = 0;
            return false;
        }

        if (mode_ == MramFileMode::PayloadOnly && !validate_record(*entry))
        {
            size = 0;
            return false;
        }

        const std::size_t max_len = (mode_ == MramFileMode::PayloadOnly)
                                        ? entry->payload_size
                                        : entry->size;
        const std::size_t base_off = (mode_ == MramFileMode::PayloadOnly)
                                         ? entry->payload_offset
                                         : 0;

        if (offset >= max_len)
        {
            size = 0;
            return true; // EOF
        }

        const std::size_t bytes_to_read = std::min(size, max_len - offset);
        if (bytes_to_read == 0)
        {
            size = 0;
            return true;
        }

        if (!buffer)
        {
            size = 0;
            return false;
        }

        const bool ok = backend_.readRecord(*entry, base_off + offset, buffer, bytes_to_read);
        size = ok ? bytes_to_read : 0;
        return ok;
    }

private:
    struct Header
    {
        std::uint32_t version;
        std::uint32_t crc;
        std::uint32_t seq;
        std::uint32_t flags;
    };

    bool validate_record(const MramRegistryEntry& entry) const
    {
        Header hdr{};
        if (!backend_.readRecord(entry, 0, reinterpret_cast<std::uint8_t*>(&hdr), sizeof(hdr)))
            return false;
        if (hdr.version != entry.version)
            return false;

        std::uint32_t crc = 0xFFFFFFFFu;
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.version), sizeof(hdr.version));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.seq),     sizeof(hdr.seq));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.flags),   sizeof(hdr.flags));

        std::uint8_t chunk[64]{};
        std::size_t done = 0;
        while (done < entry.payload_size)
        {
            const std::size_t n = std::min(sizeof(chunk), entry.payload_size - done);
            if (!backend_.readRecord(entry, entry.payload_offset + done, chunk, n))
                return false;
            crc = detail::crc32_update(crc, chunk, n);
            done += n;
        }
        return (crc ^ 0xFFFFFFFFu) == hdr.crc;
    }

    const Backend& backend_;
    MramFileMode   mode_;
};

#endif // __MRAM_FILE_ACCESS_H__