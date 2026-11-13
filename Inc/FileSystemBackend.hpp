#pragma once
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "Mram.hpp"
#include "MramStorageBackend.hpp"

class PerRecordFileBackend
{
public:
    explicit PerRecordFileBackend(std::filesystem::path root_dir)
        : root_(std::move(root_dir))
    {
        std::filesystem::create_directories(root_);
    }

    bool readRecord(const MramRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!entry.name || entry.name[0] == '\0')
            return false;
        if (!mram_validate_range(entry, offset, len))
            return false;
        if (len == 0)
            return true;
        if (!dst)
            return false;
        if (offset > static_cast<std::size_t>(std::numeric_limits<long>::max()))
            return false;

        std::FILE* f = std::fopen(record_path(entry).c_str(), "rb");
        if (!f)
            return false;

        const bool ok = (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0) &&
                        (std::fread(dst, 1, len, f) == len);
        std::fclose(f);
        return ok;
    }

    bool writeRecord(const MramRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!entry.name || entry.name[0] == '\0')
            return false;
        if (!mram_validate_range(entry, offset, len))
            return false;
        if (len == 0)
            return true;
        if (!src)
            return false;
        if (offset > static_cast<std::size_t>(std::numeric_limits<long>::max()))
            return false;

        const std::string path = record_path(entry);
        std::FILE* f = std::fopen(path.c_str(), "r+b");
        if (!f)
        {
            // File does not exist yet: create & initialize to erased MRAM state (0xFF)
            f = std::fopen(path.c_str(), "w+b");
            if (!f)
                return false;

            for (std::size_t i = 0; i < entry.size; ++i)
            {
                if (std::fputc(0xFF, f) == EOF)
                {
                    std::fclose(f);
                    return false;
                }
            }
        }

        const bool ok = (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0) &&
                        (std::fwrite(src, 1, len, f) == len) &&
                        (std::fflush(f) == 0);
        std::fclose(f);
        return ok;
    }

private:
    std::string record_path(const MramRegistryEntry& entry) const
    {
        return (root_ / (std::string(entry.name) + ".bin")).string();
    }

    std::filesystem::path root_;
};

static_assert(MramStorageBackend<PerRecordFileBackend>);

class FlatFileBackend
{
public:
    FlatFileBackend(std::filesystem::path image_path, std::size_t total_size)
        : path_(std::move(image_path)), total_size_(total_size)
    {
        if (!std::filesystem::exists(path_))
        {
            if (path_.has_parent_path())
                std::filesystem::create_directories(path_.parent_path());

            if (std::FILE* f = std::fopen(path_.string().c_str(), "wb"))
            {
                for (std::size_t i = 0; i < total_size_; ++i)
                {
                    if (std::fputc(0xFF, f) == EOF)
                        break;
                }
                std::fclose(f);
            }
        }
    }

    bool readRecord(const MramRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!mram_validate_range(entry, offset, len))
            return false;
        if (entry.offset > total_size_ ||
            offset > total_size_ - entry.offset ||
            len > total_size_ - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!dst)
            return false;

        const std::size_t addr = entry.offset + offset;
        if (addr > static_cast<std::size_t>(std::numeric_limits<long>::max()))
            return false;

        std::FILE* f = std::fopen(path_.string().c_str(), "rb");
        if (!f)
            return false;

        const bool ok = (std::fseek(f, static_cast<long>(addr), SEEK_SET) == 0) &&
                        (std::fread(dst, 1, len, f) == len);
        std::fclose(f);
        return ok;
    }

    bool writeRecord(const MramRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!mram_validate_range(entry, offset, len))
            return false;
        if (entry.offset > total_size_ ||
            offset > total_size_ - entry.offset ||
            len > total_size_ - entry.offset - offset)
            return false;
        if (len == 0)
            return true;
        if (!src)
            return false;

        const std::size_t addr = entry.offset + offset;
        if (addr > static_cast<std::size_t>(std::numeric_limits<long>::max()))
            return false;

        std::FILE* f = std::fopen(path_.string().c_str(), "r+b");
        if (!f)
            return false;

        const bool ok = (std::fseek(f, static_cast<long>(addr), SEEK_SET) == 0) &&
                        (std::fwrite(src, 1, len, f) == len) &&
                        (std::fflush(f) == 0);
        std::fclose(f);
        return ok;
    }

private:
    std::filesystem::path path_;
    std::size_t           total_size_;
};

static_assert(MramStorageBackend<FlatFileBackend>);