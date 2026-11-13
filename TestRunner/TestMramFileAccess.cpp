#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Mram.hpp"
#include "MramFileAccess.hpp"
#include "MramStorageBackend.hpp"
#include "MramStore.hpp"
#include "MramWriteStream.hpp"
#include "RamBufferBackend.hpp"
#include "RamMramMock.hpp"

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
    MramRecord<mram_config_t> config;
    MramRecord<mram_state_t>  state;
};

// ------------------------------------------------------------
// Member list
// ------------------------------------------------------------
template <>
struct MramLayoutMembers<MramLayout>
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
struct MramTraits<mram_config_t, MramLayout>
{
    static constexpr std::uint32_t offset  = offsetof(MramLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "config";
};

template <>
struct MramTraits<mram_state_t, MramLayout>
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

    bool readRecord(const MramRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (fail_read || !mram_validate_range(entry, offset, len))
            return false;
        if (len > 0)
            std::memcpy(dst, storage.data() + entry.offset + offset, len);
        return true;
    }

    bool writeRecord(const MramRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!mram_validate_range(entry, offset, len))
            return false;
        if (len > 0)
            std::memcpy(storage.data() + entry.offset + offset, src, len);
        return true;
    }
};

static_assert(MramStorageBackend<FailingFileAccessBackend>);

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
TEST_CASE("MramFileAccess (RamBufferBackend, RawRecord): full and partial reads")
{
    std::array<std::uint8_t, sizeof(MramLayout)> buffer{};
    auto* layout = reinterpret_cast<MramLayout*>(buffer.data());

    auto* cfg_bytes = reinterpret_cast<std::uint8_t*>(&layout->config);
    for (std::size_t i = 0; i < sizeof(MramRecord<mram_config_t>); ++i)
        cfg_bytes[i] = static_cast<std::uint8_t>(0x10 + i);

    auto* st_bytes = reinterpret_cast<std::uint8_t*>(&layout->state);
    for (std::size_t i = 0; i < sizeof(MramRecord<mram_state_t>); ++i)
        st_bytes[i] = static_cast<std::uint8_t>(0x50 + i);

    RamBufferBackend backend(buffer.data(), buffer.size());
    MramFileAccess<MramLayout, RamBufferBackend> access(backend, MramFileMode::RawRecord);

    SUBCASE("Full read")
    {
        std::uint8_t out[32]{};
        std::size_t size = sizeof(out);

        REQUIRE(access.read(makePath("config"), 0, out, size) == true);
        CHECK(size == sizeof(MramRecord<mram_config_t>));
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
        const std::size_t offset = sizeof(MramRecord<mram_config_t>) - 3;

        REQUIRE(access.read(makePath("config"), offset, out, size) == true);
        CHECK(size == 3);
        CHECK(out[0] == static_cast<std::uint8_t>(0x10 + offset));
        CHECK(out[2] == static_cast<std::uint8_t>(0x10 + offset + 2));
    }
}

TEST_CASE("MramFileAccess (RamMockBackend, RawRecord): EOF, missing file, and edge cases")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramFileAccess<MramLayout, decltype(backend)> access(backend, MramFileMode::RawRecord);

    SUBCASE("Offset at or beyond end returns EOF (true, size=0)")
    {
        std::uint8_t out[4]{};
        std::size_t size = sizeof(out);

        CHECK(access.read(makePath("config"), sizeof(MramRecord<mram_config_t>), out, size) == true);
        CHECK(size == 0);

        size = sizeof(out);
        CHECK(access.read(makePath("config"), sizeof(MramRecord<mram_config_t>) + 10, out, size) == true);
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
// PayloadOnly mode + MramStore / MramWriteStream interop
// ------------------------------------------------------------
TEST_CASE("MramFileAccess (PayloadOnly): reads validated payload written by MramStore or MramWriteStream")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);
    MramFileAccess<MramLayout, decltype(backend)> access(backend, MramFileMode::PayloadOnly);

    // 1. Erased MRAM fails validation
    {
        std::uint8_t out[16]{};
        std::size_t size = sizeof(out);
        CHECK(access.read(makePath("config"), 0, out, size) == false);
        CHECK(size == 0);
    }

    // 2. Write via MramStore, then read chunked payload via MramFileAccess
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

    // 3. Write via MramWriteStream(PayloadOnly), then read via MramFileAccess(PayloadOnly)
    MramWriteStream<MramLayout, decltype(backend)> writer(backend, MramStreamMode::PayloadOnly);
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

TEST_CASE("MramFileAccess (PayloadOnly): rejects corrupted payload or mismatched version")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);
    MramFileAccess<MramLayout, decltype(backend)> access(backend, MramFileMode::PayloadOnly);

    REQUIRE(store.set(mram_config_t{{1, 2, 3, 4}}) == true);

    // Flip one byte in stored payload
    mock.data()[MramTraits<mram_config_t, MramLayout>::offset +
                offsetof(MramRecord<mram_config_t>, payload)] ^= 0x01;

    std::uint8_t out[4]{};
    std::size_t size = sizeof(out);
    CHECK(access.read(makePath("config"), 0, out, size) == false);
    CHECK(size == 0);
}

// ------------------------------------------------------------
// FailingBackend tests
// ------------------------------------------------------------
TEST_CASE("MramFileAccess (FailingBackend): propagates backend read failures")
{
    FailingFileAccessBackend backend;
    MramStore<MramLayout, FailingFileAccessBackend> store(backend);
    REQUIRE(store.set(mram_config_t{{5, 6, 7, 8}}) == true);

    backend.fail_read = true;

    SUBCASE("RawRecord mode")
    {
        MramFileAccess<MramLayout, FailingFileAccessBackend> access(backend, MramFileMode::RawRecord);
        std::uint8_t out[16]{};
        std::size_t size = sizeof(out);
        CHECK(access.read(makePath("config"), 0, out, size) == false);
        CHECK(size == 0);
    }

    SUBCASE("PayloadOnly mode")
    {
        MramFileAccess<MramLayout, FailingFileAccessBackend> access(backend, MramFileMode::PayloadOnly);
        std::uint8_t out[4]{};
        std::size_t size = sizeof(out);
        CHECK(access.read(makePath("config"), 0, out, size) == false);
        CHECK(size == 0);
    }
}