#ifndef __MRAM_WRITE_STREAM_H__
#define __MRAM_WRITE_STREAM_H__

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "InputOutputStream.hpp"
#include "Mram.hpp"
#include "MramStorageBackend.hpp"
#include "MramStore.hpp"

enum class MramStreamMode
{
    PayloadOnly, // Streamed bytes = T; finalize() writes MramRecord header (version/seq/crc)
    RawRecord    // Streamed bytes = raw MramRecord<T> (header + payload)
};

template <typename Layout, MramStorageBackend Backend>
class MramWriteStream
{
public:
    explicit MramWriteStream(Backend& backend,
                             MramStreamMode mode = MramStreamMode::RawRecord)
        : backend_(backend), mode_(mode) {}

    bool initialize(const std::array<char, NAME_LENGTH>& name)
    {
        const auto it = std::find(name.begin(), name.end(), '\0');
        const std::string_view path(name.data(), static_cast<std::size_t>(it - name.begin()));

        entry_    = MramRegistry<Layout>::lookup(path);
        offset_   = 0;
        next_seq_ = 1;

        if (!entry_)
            return false;

        if (mode_ == MramStreamMode::PayloadOnly)
            next_seq_ = compute_next_seq();

        return true;
    }

    bool output(const std::uint8_t* data, std::size_t size)
    {
        if (!entry_)
            return false;
        if (size == 0)
            return true;
        if (!data)
            return false;

        const std::size_t max_len = (mode_ == MramStreamMode::PayloadOnly)
                                        ? entry_->payload_size
                                        : entry_->size;
        const std::size_t base_off = (mode_ == MramStreamMode::PayloadOnly)
                                         ? entry_->payload_offset
                                         : 0;

        if (offset_ > max_len || size > max_len - offset_)
            return false;

        if (!backend_.writeRecord(*entry_, base_off + offset_, data, size))
            return false;

        offset_ += size;
        return true;
    }

    bool finalize()
    {
        if (!entry_)
            return false;

        bool ok = true;
        if (mode_ == MramStreamMode::PayloadOnly)
            ok = (offset_ == entry_->payload_size) && commit_header();

        entry_    = nullptr;
        offset_   = 0;
        next_seq_ = 1;
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

    std::uint32_t compute_next_seq() const
    {
        Header prev{};
        if (!backend_.readRecord(*entry_, 0, reinterpret_cast<std::uint8_t*>(&prev), sizeof(prev)))
            return 1;
        if (prev.version != entry_->version)
            return 1;

        std::uint32_t expected_crc = 0;
        if (!compute_payload_crc(prev.version, prev.seq, prev.flags, expected_crc))
            return 1;
        if (expected_crc != prev.crc)
            return 1;

        return prev.seq + 1;
    }

    bool commit_header()
    {
        Header hdr{};
        hdr.version = entry_->version;
        hdr.seq     = next_seq_;
        hdr.flags   = 0;
        if (!compute_payload_crc(hdr.version, hdr.seq, hdr.flags, hdr.crc))
            return false;

        return backend_.writeRecord(*entry_, 0,
                                    reinterpret_cast<const std::uint8_t*>(&hdr),
                                    sizeof(hdr));
    }

    bool compute_payload_crc(std::uint32_t version,
                             std::uint32_t seq,
                             std::uint32_t flags,
                             std::uint32_t& crc_out) const
    {
        std::uint32_t crc = 0xFFFFFFFFu;
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&version), sizeof(version));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&seq),     sizeof(seq));
        crc = detail::crc32_update(crc, reinterpret_cast<const std::uint8_t*>(&flags),   sizeof(flags));

        std::uint8_t chunk[64]{};
        std::size_t done = 0;
        while (done < entry_->payload_size)
        {
            const std::size_t n = std::min(sizeof(chunk), entry_->payload_size - done);
            if (!backend_.readRecord(*entry_, entry_->payload_offset + done, chunk, n))
                return false;
            crc = detail::crc32_update(crc, chunk, n);
            done += n;
        }
        crc_out = crc ^ 0xFFFFFFFFu;
        return true;
    }

    Backend&                 backend_;
    MramStreamMode           mode_;
    const MramRegistryEntry* entry_    = nullptr;
    std::size_t              offset_   = 0;
    std::uint32_t            next_seq_ = 1;
};

#endif // __MRAM_WRITE_STREAM_H__