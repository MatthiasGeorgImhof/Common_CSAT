#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "Mram.hpp"
#include "MramStorageBackend.hpp"
#include "MramStore.hpp"
#include "RamBufferBackend.hpp"
#include "RamMramMock.hpp"

// ------------------------------------------------------------
// Test layouts
// ------------------------------------------------------------
struct SmallConfig { std::uint32_t a; std::uint32_t b; };
struct SmallState  { std::uint8_t  x[12]; };

struct TestLayout
{
    MramRecord<SmallConfig> config;
    MramRecord<SmallState>  state;
};

template <>
struct MramLayoutMembers<TestLayout>
{
    static constexpr auto members = std::make_tuple(
        &TestLayout::config,
        &TestLayout::state
    );
};

template <>
struct MramTraits<SmallConfig, TestLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "config";
};

template <>
struct MramTraits<SmallState, TestLayout>
{
    static constexpr std::uint32_t offset  = offsetof(TestLayout, state);
    static constexpr std::uint32_t version = 2;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "state";
};

// Large layout (> 65535 bytes) to exercise RamMockBackend chunking
struct LargeBlob { std::uint8_t bytes[70000]; };

struct LargeLayout
{
    MramRecord<LargeBlob> blob;
};

template <>
struct MramLayoutMembers<LargeLayout>
{
    static constexpr auto members = std::make_tuple(&LargeLayout::blob);
};

template <>
struct MramTraits<LargeBlob, LargeLayout>
{
    static constexpr std::uint32_t offset  = offsetof(LargeLayout, blob);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "blob";
};

static_assert(MramStorageBackend<RamBufferBackend>);
static_assert(MramStorageBackend<RamMockBackend<sizeof(TestLayout)>>);
static_assert(MramStorageBackend<RamMockBackend<sizeof(LargeLayout)>>);

// ------------------------------------------------------------
// RamMramMock tests
// ------------------------------------------------------------
TEST_CASE("RamMramMock: defaults to 0xFF and supports reset")
{
    RamMramMock<64> mock;
    for (std::size_t i = 0; i < mock.kSize; ++i)
        CHECK(mock.data()[i] == 0xFF);

    mock.reset(0x5A);
    for (std::size_t i = 0; i < mock.kSize; ++i)
        CHECK(mock.data()[i] == 0x5A);
}

TEST_CASE("RamMramMock: readBytes and writeBytes round-trip and bounds checks")
{
    RamMramMock<32> mock;
    const std::uint8_t in[4] = {0x11, 0x22, 0x33, 0x44};
    std::uint8_t out[4]{};

    CHECK(mock.writeBytes(10, in, 4) == true);
    CHECK(mock.readBytes(10, out, 4) == true);
    CHECK(std::memcmp(in, out, 4) == 0);

    // Zero-length at valid end offset (even with nullptr)
    CHECK(mock.writeBytes(32, nullptr, 0) == true);
    CHECK(mock.readBytes(32, nullptr, 0) == true);

    // Non-zero length with nullptr rejected
    CHECK(mock.writeBytes(0, nullptr, 1) == false);
    CHECK(mock.readBytes(0, nullptr, 1) == false);

    // Out-of-bounds and wraparound checks
    CHECK(mock.writeBytes(30, in, 4) == false);
    CHECK(mock.readBytes(30, out, 4) == false);
    CHECK(mock.writeBytes(std::numeric_limits<std::uint32_t>::max() - 1, in, 4) == false);
    CHECK(mock.readBytes(std::numeric_limits<std::uint32_t>::max() - 1, out, 4) == false);
}

// ------------------------------------------------------------
// RamBufferBackend tests
// ------------------------------------------------------------
TEST_CASE("RamBufferBackend: read/write full and partial records")
{
    std::array<std::uint8_t, sizeof(TestLayout)> buf{};
    buf.fill(0xFF);
    RamBufferBackend backend(buf.data(), buf.size());

    constexpr auto& cfg_entry = MramRegistry<TestLayout>::entry_for<SmallConfig>();
    constexpr auto& st_entry  = MramRegistry<TestLayout>::entry_for<SmallState>();

    std::vector<std::uint8_t> in(cfg_entry.size);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<std::uint8_t>(i + 1);

    REQUIRE(backend.writeRecord(cfg_entry, 0, in.data(), in.size()) == true);

    // Partial read inside record
    std::uint8_t partial[4]{};
    REQUIRE(backend.readRecord(cfg_entry, 5, partial, sizeof(partial)) == true);
    CHECK(partial[0] == 6);
    CHECK(partial[3] == 9);

    // Adjacent record remains untouched (0xFF)
    std::vector<std::uint8_t> st_bytes(st_entry.size);
    REQUIRE(backend.readRecord(st_entry, 0, st_bytes.data(), st_bytes.size()) == true);
    for (auto b : st_bytes)
        CHECK(b == 0xFF);
}

TEST_CASE("RamBufferBackend: rejects null pointers, record overflow, and buffer overflow")
{
    std::array<std::uint8_t, sizeof(TestLayout)> buf{};
    constexpr auto& cfg_entry = MramRegistry<TestLayout>::entry_for<SmallConfig>();
    constexpr auto& st_entry  = MramRegistry<TestLayout>::entry_for<SmallState>();

    SUBCASE("Null base pointer")
    {
        RamBufferBackend null_backend(nullptr, 1024);
        std::uint8_t b = 0;
        CHECK(null_backend.writeRecord(cfg_entry, 0, &b, 1) == false);
        CHECK(null_backend.readRecord(cfg_entry, 0, &b, 1) == false);
    }

    SUBCASE("Null src/dst with non-zero len")
    {
        RamBufferBackend backend(buf.data(), buf.size());
        CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 1) == false);
        CHECK(backend.readRecord(cfg_entry, 0, nullptr, 1) == false);
        CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 0) == true);
        CHECK(backend.readRecord(cfg_entry, 0, nullptr, 0) == true);
    }

    SUBCASE("Exceeding record size")
    {
        RamBufferBackend backend(buf.data(), buf.size());
        std::vector<std::uint8_t> tmp(cfg_entry.size + 1, 0xAA);
        CHECK(backend.writeRecord(cfg_entry, 0, tmp.data(), tmp.size()) == false);
        CHECK(backend.readRecord(cfg_entry, 1, tmp.data(), cfg_entry.size) == false);
    }

    SUBCASE("Backing buffer smaller than layout")
    {
        std::array<std::uint8_t, sizeof(MramRecord<SmallConfig>) + 4> small_buf{};
        RamBufferBackend backend(small_buf.data(), small_buf.size());
        std::vector<std::uint8_t> tmp(st_entry.size, 0x55);

        // config fits, state does not fit in small_buf
        CHECK(backend.writeRecord(cfg_entry, 0, tmp.data(), cfg_entry.size) == true);
        CHECK(backend.writeRecord(st_entry, 0, tmp.data(), st_entry.size) == false);
        CHECK(backend.readRecord(st_entry, 0, tmp.data(), st_entry.size) == false);
    }
}

// ------------------------------------------------------------
// RamMockBackend tests
// ------------------------------------------------------------
TEST_CASE("RamMockBackend: chunked read/write over 64 KiB record")
{
    RamMramMock<sizeof(LargeLayout)> mock;
    RamMockBackend backend(mock);

    constexpr auto& entry = MramRegistry<LargeLayout>::entry_for<LargeBlob>();
    REQUIRE(entry.size > 65535);

    std::vector<std::uint8_t> in(entry.size);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<std::uint8_t>((i * 131u) & 0xFFu);

    REQUIRE(backend.writeRecord(entry, 0, in.data(), in.size()) == true);

    std::vector<std::uint8_t> out(entry.size, 0);
    REQUIRE(backend.readRecord(entry, 0, out.data(), out.size()) == true);
    CHECK(out == in);
}

TEST_CASE("RamMockBackend: rejects backing mock smaller than layout and invalid ranges")
{
    RamMramMock<sizeof(MramRecord<SmallConfig>)> tiny_mock;
    RamMockBackend backend(tiny_mock);

    constexpr auto& st_entry = MramRegistry<TestLayout>::entry_for<SmallState>();
    std::uint8_t tmp[16]{};

    CHECK(backend.writeRecord(st_entry, 0, tmp, sizeof(tmp)) == false);
    CHECK(backend.readRecord(st_entry, 0, tmp, sizeof(tmp)) == false);
    CHECK(backend.writeRecord(st_entry, 0, nullptr, 1) == false);
    CHECK(backend.readRecord(st_entry, 0, nullptr, 1) == false);
}

// ------------------------------------------------------------
// End-to-end MramStore integration with both RAM backends
// ------------------------------------------------------------
TEST_CASE("MramStore works identically with RamBufferBackend and RamMockBackend")
{
    std::array<std::uint8_t, sizeof(TestLayout)> raw_buf{};
    raw_buf.fill(0xFF);
    RamBufferBackend buf_backend(raw_buf.data(), raw_buf.size());

    RamMramMock<sizeof(TestLayout)> mock;
    RamMockBackend mock_backend(mock);

    MramStore<TestLayout, RamBufferBackend> store1(buf_backend);
    MramStore<TestLayout, RamMockBackend<sizeof(TestLayout)>> store2(mock_backend);

    const SmallConfig cfg{1000000, 99};
    REQUIRE(store1.set(cfg, 0x01) == true);
    REQUIRE(store2.set(cfg, 0x01) == true);

    SmallConfig out1{}, out2{};
    REQUIRE(store1.get(out1) == true);
    REQUIRE(store2.get(out2) == true);

    CHECK(out1.a == cfg.a);
    CHECK(out1.b == cfg.b);
    CHECK(out2.a == cfg.a);
    CHECK(out2.b == cfg.b);
    CHECK(std::memcmp(raw_buf.data(), mock.data(), sizeof(MramRecord<SmallConfig>)) == 0);
}