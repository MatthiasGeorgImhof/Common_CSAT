#pragma once
#include <cstddef>
#include <cstdint>
#include <concepts>
#include "Mram.hpp"

template <typename Backend>
concept MramStorageBackend = requires(Backend& b, const Backend& cb,
                                      const MramRegistryEntry& entry,
                                      std::size_t offset,
                                      std::uint8_t* dst,
                                      const std::uint8_t* src,
                                      std::size_t len)
{
    { cb.readRecord(entry, offset, dst, len) } -> std::same_as<bool>;
    { b.writeRecord(entry, offset, src, len) } -> std::same_as<bool>;
};

inline bool mram_validate_range(const MramRegistryEntry& entry,
                                std::size_t offset,
                                std::size_t len)
{
    return offset <= entry.size && len <= (entry.size - offset);
}