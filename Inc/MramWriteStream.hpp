#ifndef __MRAM_WRITE_STREAM_H__
#define __MRAM_WRITE_STREAM_H__

#include <cstdint>
#include <cstddef>
#include <array>
#include <cstring>

#include "InputOutputStream.hpp"
#include "Mram.hpp"

template <typename Layout>
class MramWriteStream
{
public:
    explicit MramWriteStream(std::uint8_t *mram_base)
        : base_(mram_base), current_entry_(nullptr), offset_(0) {}

    bool initialize(const std::array<char, NAME_LENGTH> &name)
    {
        std::string_view path(name.data());
        current_entry_ = MramRegistry<Layout>::lookup(path);
        offset_ = 0;
        return (current_entry_ != nullptr);
    }

    bool output(std::uint8_t *data, std::size_t size)
    {
        if (!current_entry_)
            return false;
        if (offset_ + size > current_entry_->size)
        {
            // truncate or reject; your choice
            size = current_entry_->size - offset_;
        }
        std::memcpy(base_ + current_entry_->offset + offset_, data, size);
        offset_ += size;
        return true;
    }

    bool finalize()
    {
        // here you can update version/seq/crc in the MramRecord header
        current_entry_ = nullptr;
        offset_ = 0;
        return true;
    }

private:
    std::uint8_t *base_;
    const MramRegistryEntry *current_entry_;
    std::size_t offset_;
};

template <typename Layout, typename MramTransport>
class MramChipWriteStream
{
public:
    explicit MramChipWriteStream(const MramTransport &chip) : chip_(chip), entry_(nullptr), offset_(0) {}

    bool initialize(const std::array<char, NAME_LENGTH> &name)
    {
        std::string_view path(name.data());
        entry_ = MramRegistry<Layout>::lookup(path);
        offset_ = 0;
        return entry_ != nullptr;
    }

    bool output(std::uint8_t *data, std::size_t size)
    {
        if (!entry_)
            return false;

        if (offset_ + size > entry_->size)
            size = entry_->size - offset_; // truncate or reject

        uint32_t chip_addr = static_cast<uint32_t>(entry_->offset + offset_);
        uint16_t chip_len = static_cast<uint16_t>(size);

        bool ok = chip_.writeBytes(chip_addr, data, chip_len);
        offset_ += chip_len;
        return ok;
    }

    bool finalize()
    {
        // Optional: update version/seq/crc in the MramRecord header
        entry_ = nullptr;
        offset_ = 0;
        return true;
    }

private:
    const MramTransport &chip_;
    const MramRegistryEntry *entry_;
    std::size_t offset_;
};

#endif // __MRAM_WRITE_STREAM_H__