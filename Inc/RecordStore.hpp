#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include "RecordRegistry.hpp"
#include "StorageBackend.hpp"

namespace detail
{
inline std::uint32_t crc32_update(std::uint32_t crc,
                                  const std::uint8_t* data,
                                  std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
        {
            const std::uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

template <typename T>
inline std::uint32_t compute_record_crc32(const StorageRecord<T>& rec)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&rec.version), sizeof(rec.version));
    crc = crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&rec.seq),     sizeof(rec.seq));
    crc = crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&rec.flags),   sizeof(rec.flags));
    crc = crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&rec.payload), sizeof(T));
    return crc ^ 0xFFFFFFFFu;
}
} // namespace detail

template <typename Layout, StorageBackend Backend>
class RecordStore
{
public:
    explicit RecordStore(Backend& backend) : backend_(backend) {}

    template <typename T>
    bool get_record(StorageRecord<T>& out) const
    {
        constexpr const auto& entry = RecordRegistry<Layout>::template entry_for<T>();

        StorageRecord<T> rec{};
        if (!backend_.readRecord(entry, 0, reinterpret_cast<std::uint8_t*>(&rec), sizeof(rec)))
            return false;

        if (rec.version != RecordTraits<T, Layout>::version)
            return false;

        if (rec.crc != detail::compute_record_crc32(rec))
            return false;

        out = rec;
        return true;
    }

    template <typename T>
    bool get(T& out) const
    {
        StorageRecord<T> rec{};
        if (!get_record(rec))
            return false;

        out = rec.payload;
        return true;
    }

    template <typename T>
    bool set(const T& in, std::uint32_t flags = 0)
    {
        std::uint32_t next_seq = 1;
        StorageRecord<T> prev{};
        if (get_record(prev))
            next_seq = prev.seq + 1;

        StorageRecord<T> rec{};
        rec.version = RecordTraits<T, Layout>::version;
        rec.seq     = next_seq;
        rec.flags   = flags;
        rec.payload = in;
        rec.crc     = detail::compute_record_crc32(rec);

        constexpr const auto& entry = RecordRegistry<Layout>::template entry_for<T>();
        return backend_.writeRecord(entry, 0, reinterpret_cast<const std::uint8_t*>(&rec), sizeof(rec));
    }


    bool get_raw(const RecordRegistryEntry& entry, void* payload_out) const
    {
        if (!payload_out)
            return false;

        struct Header { std::uint32_t version, crc, seq, flags; } hdr{};
        if (!backend_.readRecord(entry, 0, reinterpret_cast<std::uint8_t*>(&hdr), sizeof(hdr)))
            return false;
        if (hdr.version != entry.version)
            return false;

        auto* dst = static_cast<std::uint8_t*>(payload_out);
        if (!backend_.readRecord(entry, entry.payload_offset, dst, entry.payload_size))
            return false;

        std::uint32_t crc = 0xFFFFFFFFu;
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.version), sizeof(hdr.version));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.seq),     sizeof(hdr.seq));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.flags),   sizeof(hdr.flags));
        crc = detail::crc32_update(crc, dst, entry.payload_size);

        return (crc ^ 0xFFFFFFFFu) == hdr.crc;
    }

    bool set_raw(const RecordRegistryEntry& entry, const void* payload_in, std::uint32_t flags = 0)
    {
        if (!payload_in)
            return false;

        struct Header { std::uint32_t version, crc, seq, flags; } prev{}, hdr{};
        std::uint32_t next_seq = 1;

        // Check existing record to continue seq if valid
        if (backend_.readRecord(entry, 0, reinterpret_cast<std::uint8_t*>(&prev), sizeof(prev)) &&
            prev.version == entry.version)
        {
            std::uint8_t chunk[64]{};
            std::uint32_t crc = 0xFFFFFFFFu;
            crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&prev.version), sizeof(prev.version));
            crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&prev.seq),     sizeof(prev.seq));
            crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&prev.flags),   sizeof(prev.flags));

            bool ok = true;
            for (std::size_t done = 0; done < entry.payload_size; )
            {
                const std::size_t n = std::min(sizeof(chunk), entry.payload_size - done);
                if (!backend_.readRecord(entry, entry.payload_offset + done, chunk, n))
                {
                    ok = false;
                    break;
                }
                crc = detail::crc32_update(crc, chunk, n);
                done += n;
            }
            if (ok && (crc ^ 0xFFFFFFFFu) == prev.crc)
                next_seq = prev.seq + 1;
        }

        const auto* src = static_cast<const std::uint8_t*>(payload_in);
        hdr.version = entry.version;
        hdr.seq     = next_seq;
        hdr.flags   = flags;

        std::uint32_t crc = 0xFFFFFFFFu;
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.version), sizeof(hdr.version));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.seq),     sizeof(hdr.seq));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&hdr.flags),   sizeof(hdr.flags));
        crc = detail::crc32_update(crc, src, entry.payload_size);
        hdr.crc = crc ^ 0xFFFFFFFFu;

        return backend_.writeRecord(entry, entry.payload_offset, src, entry.payload_size) &&
            backend_.writeRecord(entry, 0, reinterpret_cast<const std::uint8_t*>(&hdr), sizeof(hdr));
    }

private:
    Backend& backend_;
};