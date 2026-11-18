#pragma once
#include <cstddef>
#include <cstdint>
#include <concepts>
#include "RecordRegistry.hpp"

enum class RecordAccessMode
{
    PayloadOnly, // Streamed bytes = T; finalize() writes StorageRecord header (version/seq/crc)
                 // Reads validated payload T (requires matching version & CRC)
                 
    RawRecord    // Streamed bytes = raw StorageRecord<T> (header + payload)
                 // Reads raw StorageRecord<T> bytes (header + payload)
};

template <typename Backend>
concept StorageBackend = requires(Backend &b, const Backend &cb,
                                  const RecordRegistryEntry &entry,
                                  std::size_t offset,
                                  std::uint8_t *dst,
                                  const std::uint8_t *src,
                                  std::size_t len) {
    { cb.readRecord(entry, offset, dst, len) } -> std::same_as<bool>;
    { b.writeRecord(entry, offset, src, len) } -> std::same_as<bool>;
};

inline bool validate_record_range(const RecordRegistryEntry &entry,
                                std::size_t offset,
                                std::size_t len)
{
    return offset <= entry.size && len <= (entry.size - offset);
}