#ifndef __MRAM_FILE_ACCESS_H__
#define __MRAM_FILE_ACCESS_H__

#include <cstdint>
#include <cstddef>
#include <array>
#include <cstring>

#include "FileAccess.hpp"
#include "Mram.hpp"

class MramFileAccess
{
public:
    explicit MramFileAccess(std::uint8_t* mram_base) : base_(mram_base) {}

    template <typename Layout>
    bool read(const FileName& path_array,
              std::size_t offset,
              std::uint8_t* buffer,
              std::size_t& size)
    {
        std::string_view path(path_array.data());
        auto* entry = MramRegistry<Layout>::lookup(path);
        if (!entry) {
            size = 0;
            return false;
        }

        if (offset >= entry->size) {
            size = 0;
            return true; // EOF
        }

        std::size_t bytes_to_read = std::min(size, entry->size - offset);
        std::memcpy(buffer, base_ + entry->offset + offset, bytes_to_read);
        size = bytes_to_read;
        return true;
    }

private:
    std::uint8_t* base_; // pointer to MRAM memory-mapped region or driver buffer
};

template <typename Layout, typename MramTransport>
class MramChipFileAccess
{
public:
    explicit MramChipFileAccess(const MramTransport& chip) : chip_(chip) {}

    bool read(const FileName& path_array,
              std::size_t offset,
              std::uint8_t* buffer,
              std::size_t& size)
    {
        std::string_view path(path_array.data());
        auto* entry = MramRegistry<Layout>::lookup(path);
        if (!entry) {
            size = 0;
            return false;
        }

        if (offset >= entry->size) {
            size = 0;
            return true; // EOF
        }

        std::size_t bytes_to_read = std::min(size, entry->size - offset);

        uint32_t chip_addr = static_cast<uint32_t>(entry->offset + offset);
        uint16_t chip_len  = static_cast<uint16_t>(bytes_to_read);

        bool ok = chip_.readBytes(chip_addr, buffer, chip_len);
        size = ok ? bytes_to_read : 0;
        return ok;
    }

private:
    const MramTransport& chip_;
};

#endif // __MRAM_FILE_ACCESS_H__