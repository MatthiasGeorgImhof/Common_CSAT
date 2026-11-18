#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "RecordRegistry.hpp"
#include "StoreFileAccess.hpp"
#include "StorageBackend.hpp"
#include "RecordStore.hpp"
#include "StoreWriteStream.hpp"
#include "RamBufferBackend.hpp"
#include "RamStorageMock.hpp"

// ------------------------------------------------------------
// Dummy payload types
// ------------------------------------------------------------
struct mram_config_t { std::uint8_t a[4]; };
struct mram_state_t  { std::uint8_t b[8]; };

// ------------------------------------------------------------
// Mission-specific layout
// ------------------------------------------------------------
struct MramLayout
{
    StorageRecord<mram_config_t> config;
    StorageRecord<mram_state_t>  state;
};

// ------------------------------------------------------------
// Member list
// ------------------------------------------------------------
template <>
struct LayoutMembers<MramLayout>
{
    static constexpr auto members = std::make_tuple(
        &MramLayout::config,
        &MramLayout::state
    );
};

// ------------------------------------------------------------
// Traits
// ------------------------------------------------------------
template <>
struct RecordTraits<mram_config_t, MramLayout>
{
    static constexpr std::uint32_t offset  = offsetof(MramLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "config";
};

template <>
struct RecordTraits<mram_state_t, MramLayout>
{
    static constexpr std::uint32_t offset  = offsetof(MramLayout, state);
    static constexpr std::uint32_t version = 2;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "state";
};

// ------------------------------------------------------------
// Failing backend for error-propagation tests
// ------------------------------------------------------------
class FailingFileAccessBackend
{
public:
    bool fail_read = false;
    std::array<std::uint8_t, sizeof(MramLayout)> storage{};

    FailingFileAccessBackend() { storage.fill(0xFF); }

    bool readRecord(const RecordRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (fail_read || !validate_record_range(entry, offset, len))
            return false;
        if (len > 0)
            std::memcpy(dst, storage.data() + entry.offset + offset, len);
        return true;
    }

    bool writeRecord(const RecordRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!validate_record_range(entry, offset, len))
            return false;
        if (len > 0)
            std::memcpy(storage.data() + entry.offset + offset, src, len);
        return true;
    }
};

static_assert(StorageBackend<FailingFileAccessBackend>);

// ------------------------------------------------------------
// Helper: convert string to NAME_LENGTH array
// ------------------------------------------------------------
FileName makePath(const char* s)
{
    FileName arr{};
    std::snprintf(arr.data(), NAME_LENGTH, "%s", s);
    return arr;
}

// ------------------------------------------------------------
// RawRecord mode tests (RamBufferBackend & RamMockBackend)
// ------------------------------------------------------------
TEST_CASE("FileAccess (RamBufferBackend, RawRecord): full and partial reads")
{
    std::array<std::uint8_t, sizeof(MramLayout)> buffer{};
    auto* layout = reinterpret_cast<MramLayout*>(buffer.data());

    auto* cfg_bytes = reinterpret_cast<std::uint8_t*>(&layout->config);
    for (std::size_t i = 0; i < sizeof(StorageRecord<mram_config_t>); ++i)
        cfg_bytes[i] = static_cast<std::uint8_t>(0x10 + i);

    auto* st_bytes = reinterpret_cast<std::uint8_t*>(&layout->state);
    for (std::size_t i = 0; i < sizeof(StorageRecord<mram_state_t>); ++i)
        st_bytes[i] = static_cast<std::uint8_t>(0x50 + i);

    RamBufferBackend backend(buffer.data(), buffer.size());
    FileAccess<MramLayout, RamBufferBackend> access(backend, RecordAccessMode::RawRecord);

    SUBCASE("Full read")
    {
        std::uint8_t out[32]{};
        std::size_t size = sizeof(out);

        REQUIRE(access.read(makePath("config"), 0, out, size) == true);
        CHECK(size == sizeof(StorageRecord<mram_config_t>));
        CHECK(out[0] == 0x10);
        CHECK(out[size - 1] == static_cast<std::uint8_t>(0x10 + size - 1));
    }

    SUBCASE("Partial read at non-zero offset")
    {
        std::uint8_t out[4]{};
        std::size_t size = sizeof(out);
        const std::size_t offset = 5;

        REQUIRE(access.read(makePath("state"), offset, out, size) == true);
        CHECK(size == 4);
        CHECK(out[0] == static_cast<std::uint8_t>(0x50 + offset));
        CHECK(out[3] == static_cast<std::uint8_t>(0x50 + offset + 3));
    }

    SUBCASE("Tail read clamped to remaining record bytes")
    {
        std::uint8_t out[16]{};
        std::size_t size = sizeof(out);
        const std::size_t offset = sizeof(StorageRecord<mram_config_t>) - 3;

        REQUIRE(access.read(makePath("config"), offset, out, size) == true);
        CHECK(size == 3);
        CHECK(out[0] == static_cast<std::uint8_t>(0x10 + offset));
        CHECK(out[2] == static_cast<std::uint8_t>(0x10 + offset + 2));
    }
}

TEST_CASE("FileAccess (RamMockBackend, RawRecord): EOF, missing file, and edge cases")
{
    RamStorageMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    FileAccess<MramLayout, decltype(backend)> access(backend, RecordAccessMode::RawRecord);

    SUBCASE("Offset at or beyond end returns EOF (true, size=0)")
    {
        std::uint8_t out[4]{};
        std::size_t size = sizeof(out);

        CHECK(access.read(makePath("config"), sizeof(StorageRecord<mram_config_t>), out, size) == true);
        CHECK(size == 0);

        size = sizeof(out);
        CHECK(access.read(makePath("config"), sizeof(StorageRecord<mram_config_t>) + 10, out, size) == true);
        CHECK(size == 0);
    }

    SUBCASE("Missing file and non-null-terminated full-length path return false")
    {
        std::uint8_t out[4]{};
        std::size_t size = sizeof(out);
        CHECK(access.read(makePath("does_not_exist"), 0, out, size) == false);
        CHECK(size == 0);

        FileName full_name{};
        full_name.fill('z');
        size = sizeof(out);
        CHECK(access.read(full_name, 0, out, size) == false);
        CHECK(size == 0);
    }

    SUBCASE("Zero-length read succeeds and null buffer with non-zero size fails")
    {
        std::size_t zero_size = 0;
        CHECK(access.read(makePath("config"), 0, nullptr, zero_size) == true);
        CHECK(zero_size == 0);

        std::size_t nonzero_size = 4;
        CHECK(access.read(makePath("config"), 0, nullptr, nonzero_size) == false);
        CHECK(nonzero_size == 0);
    }
}

// ------------------------------------------------------------
// PayloadOnly mode + RecordStore / MramWriteStream interop
// ------------------------------------------------------------
TEST_CASE("FileAccess (PayloadOnly): reads validated payload written by RecordStore or MramWriteStream")
{
    RamStorageMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    RecordStore<MramLayout, decltype(backend)> store(backend);
    FileAccess<MramLayout, decltype(backend)> access(backend, RecordAccessMode::PayloadOnly);

    // 1. Erased MRAM fails validation
    {
        std::uint8_t out[16]{};
        std::size_t size = sizeof(out);
        CHECK(access.read(makePath("config"), 0, out, size) == false);
        CHECK(size == 0);
    }

    // 2. Write via RecordStore, then read chunked payload via FileAccess
    REQUIRE(store.set(mram_state_t{{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}}) == true);

    std::vector<std::uint8_t> assembled;
    std::size_t offset = 0;
    while (true)
    {
        std::uint8_t chunk[3]{};
        std::size_t chunk_len = sizeof(chunk);
        REQUIRE(access.read(makePath("state"), offset, chunk, chunk_len) == true);
        if (chunk_len == 0)
            break; // EOF
        assembled.insert(assembled.end(), chunk, chunk + chunk_len);
        offset += chunk_len;
    }

    REQUIRE(assembled.size() == sizeof(mram_state_t));
    CHECK(assembled == std::vector<std::uint8_t>{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80});

    // 3. Write via MramWriteStream(PayloadOnly), then read via FileAccess(PayloadOnly)
    StoreWriteStream<MramLayout, decltype(backend)> writer(backend, RecordAccessMode::PayloadOnly);
    REQUIRE(writer.initialize(makePath("config")) == true);
    const std::uint8_t cfg_bytes[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    REQUIRE(writer.output(cfg_bytes, sizeof(cfg_bytes)) == true);
    REQUIRE(writer.finalize() == true);

    std::uint8_t cfg_out[16]{};
    std::size_t cfg_size = sizeof(cfg_out);
    REQUIRE(access.read(makePath("config"), 0, cfg_out, cfg_size) == true);
    CHECK(cfg_size == sizeof(mram_config_t));
    CHECK(std::memcmp(cfg_out, cfg_bytes, sizeof(cfg_bytes)) == 0);
}

TEST_CASE("FileAccess (PayloadOnly): rejects corrupted payload or mismatched version")
{
    RamStorageMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    RecordStore<MramLayout, decltype(backend)> store(backend);
    FileAccess<MramLayout, decltype(backend)> access(backend, RecordAccessMode::PayloadOnly);

    REQUIRE(store.set(mram_config_t{{1, 2, 3, 4}}) == true);

    // Flip one byte in stored payload
    mock.data()[RecordTraits<mram_config_t, MramLayout>::offset +
                offsetof(StorageRecord<mram_config_t>, payload)] ^= 0x01;

    std::uint8_t out[4]{};
    std::size_t size = sizeof(out);
    CHECK(access.read(makePath("config"), 0, out, size) == false);
    CHECK(size == 0);
}

// ------------------------------------------------------------
// FailingBackend tests
// ------------------------------------------------------------
TEST_CASE("FileAccess (FailingBackend): propagates backend read failures")
{
    FailingFileAccessBackend backend;
    RecordStore<MramLayout, FailingFileAccessBackend> store(backend);
    REQUIRE(store.set(mram_config_t{{5, 6, 7, 8}}) == true);

    backend.fail_read = true;

    SUBCASE("RawRecord mode")
    {
        FileAccess<MramLayout, FailingFileAccessBackend> access(backend, RecordAccessMode::RawRecord);
        std::uint8_t out[16]{};
        std::size_t size = sizeof(out);
        CHECK(access.read(makePath("config"), 0, out, size) == false);
        CHECK(size == 0);
    }

    SUBCASE("PayloadOnly mode")
    {
        FileAccess<MramLayout, FailingFileAccessBackend> access(backend, RecordAccessMode::PayloadOnly);
        std::uint8_t out[4]{};
        std::size_t size = sizeof(out);
        CHECK(access.read(makePath("config"), 0, out, size) == false);
        CHECK(size == 0);
    }
}