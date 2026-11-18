#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "RecordRegistry.hpp"
#include "StorageBackend.hpp"
#include "RecordStore.hpp"
#include "StoreWriteStream.hpp"
#include "RamBufferBackend.hpp"
#include "RamStorageMock.hpp"

bool is_all_val(std::span<const std::uint8_t> buf, std::uint8_t scalar)
{
    return std::ranges::all_of(buf, [scalar](std::uint8_t b) { return b == scalar; });
}

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
class FailingWriteStreamBackend
{
public:
    bool fail_read  = false;
    bool fail_write = false;
    std::array<std::uint8_t, sizeof(MramLayout)> storage{};

    FailingWriteStreamBackend() { storage.fill(0xFF); }

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
        if (fail_write || !validate_record_range(entry, offset, len))
            return false;
        if (len > 0)
            std::memcpy(storage.data() + entry.offset + offset, src, len);
        return true;
    }
};

static_assert(StorageBackend<FailingWriteStreamBackend>);

// ------------------------------------------------------------
// Helper: convert string to NAME_LENGTH array
// ------------------------------------------------------------
std::array<char, NAME_LENGTH> makePath(const char* s)
{
    std::array<char, NAME_LENGTH> arr{};
    std::snprintf(arr.data(), NAME_LENGTH, "%s", s);
    return arr;
}

// ------------------------------------------------------------
// Tests with RamBufferBackend (RawRecord mode)
// ------------------------------------------------------------
TEST_CASE("StoreWriteStream (RamBufferBackend, RawRecord): initialize finds correct entry")
{
    std::array<std::uint8_t, sizeof(MramLayout)> buffer{};
    RamBufferBackend backend(buffer.data(), buffer.size());
    StoreWriteStream<MramLayout, RamBufferBackend> stream(backend, RecordAccessMode::RawRecord);

    CHECK(stream.initialize(makePath("config")) == true);
    CHECK(stream.initialize(makePath("state")) == true);
    CHECK(stream.initialize(makePath("does_not_exist")) == false);

    // Non-null-terminated full-length array must not read out of bounds or match
    std::array<char, NAME_LENGTH> full_name{};
    full_name.fill('x');
    CHECK(stream.initialize(full_name) == false);
}

TEST_CASE("StoreWriteStream (RamBufferBackend, RawRecord): output writes chunked raw bytes at correct offset")
{
    std::array<std::uint8_t, sizeof(MramLayout)> buffer{};
    RamBufferBackend backend(buffer.data(), buffer.size());
    StoreWriteStream<MramLayout, RamBufferBackend> stream(backend, RecordAccessMode::RawRecord);

    REQUIRE(stream.initialize(makePath("state")) == true);

    const std::uint8_t chunk1[3] = {1, 2, 3};
    const std::uint8_t chunk2[3] = {4, 5, 6};
    CHECK(stream.output(chunk1, sizeof(chunk1)) == true);
    CHECK(stream.output(chunk2, sizeof(chunk2)) == true);
    CHECK(stream.finalize() == true);

    const std::uint8_t* state_base = buffer.data() + RecordTraits<mram_state_t, MramLayout>::offset;
    CHECK(state_base[0] == 1);
    CHECK(state_base[2] == 3);
    CHECK(state_base[3] == 4);
    CHECK(state_base[5] == 6);

    // Adjacent config record remains untouched
    CHECK(is_all_val(std::span(buffer.data(), sizeof(StorageRecord<mram_config_t>)), 0));
}

TEST_CASE("StoreWriteStream (RamBufferBackend, RawRecord): rejects overflow and null data")
{
    std::array<std::uint8_t, sizeof(MramLayout)> buffer{};
    RamBufferBackend backend(buffer.data(), buffer.size());
    StoreWriteStream<MramLayout, RamBufferBackend> stream(backend, RecordAccessMode::RawRecord);

    REQUIRE(stream.initialize(makePath("config")) == true);

    std::uint8_t oversized[64]{};
    for (int i = 0; i < 64; ++i)
        oversized[i] = static_cast<std::uint8_t>(i + 1);

    CHECK(stream.output(oversized, sizeof(oversized)) == false);
    CHECK(stream.output(nullptr, 1) == false);
    CHECK(stream.output(nullptr, 0) == true); // zero-length no-op
    CHECK(is_all_val(buffer, 0));
}

TEST_CASE("StoreWriteStream (RamBufferBackend, RawRecord): finalize resets state")
{
    std::array<std::uint8_t, sizeof(MramLayout)> buffer{};
    RamBufferBackend backend(buffer.data(), buffer.size());
    StoreWriteStream<MramLayout, RamBufferBackend> stream(backend, RecordAccessMode::RawRecord);

    // Finalize before initialize fails
    CHECK(stream.finalize() == false);

    REQUIRE(stream.initialize(makePath("config")) == true);
    CHECK(stream.finalize() == true);

    const std::uint8_t data[4] = {9, 9, 9, 9};
    CHECK(stream.output(data, sizeof(data)) == false);
    CHECK(stream.finalize() == false);
}

// ------------------------------------------------------------
// Tests with RamMockBackend (RawRecord mode)
// ------------------------------------------------------------
TEST_CASE("StoreWriteStream (RamMockBackend, RawRecord): writes to mock MRAM and rejects overflow")
{
    RamStorageMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    StoreWriteStream<MramLayout, decltype(backend)> stream(backend, RecordAccessMode::RawRecord);

    REQUIRE(stream.initialize(makePath("state")) == true);

    const std::uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    CHECK(stream.output(data, sizeof(data)) == true);

    const std::uint8_t* state_base = mock.data() + RecordTraits<mram_state_t, MramLayout>::offset;
    CHECK(state_base[0] == 0xAA);
    CHECK(state_base[1] == 0xBB);
    CHECK(state_base[2] == 0xCC);
    CHECK(state_base[3] == 0xDD);

    // Overflow write rejected without corrupting remaining bytes
    std::uint8_t too_big[sizeof(StorageRecord<mram_state_t>)]{};
    CHECK(stream.output(too_big, sizeof(too_big)) == false);
    CHECK(state_base[4] == 0xFF);

    CHECK(stream.finalize() == true);
    CHECK(stream.output(data, sizeof(data)) == false);
}

// ------------------------------------------------------------
// Tests in PayloadOnly mode + RecordStore interoperability
// ------------------------------------------------------------
TEST_CASE("StoreWriteStream (PayloadOnly): streams payload chunks, commits header, and interoperates with RecordStore")
{
    RamStorageMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    RecordStore<MramLayout, decltype(backend)> store(backend);
    StoreWriteStream<MramLayout, decltype(backend)> stream(backend, RecordAccessMode::PayloadOnly);

    // 1. Stream config payload in two 2-byte chunks
    REQUIRE(stream.initialize(makePath("config")) == true);

    const std::uint8_t part1[2] = {0x10, 0x20};
    const std::uint8_t part2[2] = {0x30, 0x40};
    REQUIRE(stream.output(part1, sizeof(part1)) == true);

    // Header is not committed until finalize()
    mram_config_t pre_cfg{};
    CHECK(store.get(pre_cfg) == false);

    REQUIRE(stream.output(part2, sizeof(part2)) == true);
    REQUIRE(stream.finalize() == true);

    // RecordStore reads back valid record with version=1, seq=1
    StorageRecord<mram_config_t> rec{};
    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.version == 1);
    CHECK(rec.seq == 1);
    CHECK(rec.payload.a[0] == 0x10);
    CHECK(rec.payload.a[1] == 0x20);
    CHECK(rec.payload.a[2] == 0x30);
    CHECK(rec.payload.a[3] == 0x40);

    // 2. Second stream upload increments seq to 2
    REQUIRE(stream.initialize(makePath("config")) == true);
    const std::uint8_t next_payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    REQUIRE(stream.output(next_payload, sizeof(next_payload)) == true);
    REQUIRE(stream.finalize() == true);

    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.seq == 2);
    CHECK(rec.payload.a[0] == 0xAA);
    CHECK(rec.payload.a[3] == 0xDD);

    // 3. RecordStore::set followed by StoreWriteStream continues seq numbering
    REQUIRE(store.set(mram_state_t{{1, 2, 3, 4, 5, 6, 7, 8}}) == true);

    REQUIRE(stream.initialize(makePath("state")) == true);
    const std::uint8_t new_state[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    REQUIRE(stream.output(new_state, sizeof(new_state)) == true);
    REQUIRE(stream.finalize() == true);

    StorageRecord<mram_state_t> st_rec{};
    REQUIRE(store.get_record(st_rec) == true);
    CHECK(st_rec.version == 2);
    CHECK(st_rec.seq == 2);
    CHECK(st_rec.payload.b[0] == 8);
    CHECK(st_rec.payload.b[7] == 1);
}

TEST_CASE("StoreWriteStream (PayloadOnly): enforces payload_size bounds and rejects incomplete finalize")
{
    std::array<std::uint8_t, sizeof(MramLayout)> buffer{};
    buffer.fill(0xFF);
    RamBufferBackend backend(buffer.data(), buffer.size());
    RecordStore<MramLayout, RamBufferBackend> store(backend);
    StoreWriteStream<MramLayout, RamBufferBackend> stream(backend, RecordAccessMode::PayloadOnly);

    SUBCASE("Rejects writes exceeding payload_size (even if smaller than StorageRecord size)")
    {
        REQUIRE(stream.initialize(makePath("config")) == true);
        const std::uint8_t too_much[6] = {1, 2, 3, 4, 5, 6}; // payload_size is 4, record size is 20
        CHECK(stream.output(too_much, sizeof(too_much)) == false);
    }

    SUBCASE("Incomplete stream fails finalize and does not commit valid record")
    {
        REQUIRE(stream.initialize(makePath("config")) == true);
        const std::uint8_t partial[2] = {0x11, 0x22};
        REQUIRE(stream.output(partial, sizeof(partial)) == true);
        CHECK(stream.finalize() == false);

        mram_config_t cfg{};
        CHECK(store.get(cfg) == false);

        // Stream state was reset on failed finalize
        CHECK(stream.output(partial, sizeof(partial)) == false);
    }
}

// ------------------------------------------------------------
// FailingBackend tests
// ------------------------------------------------------------
TEST_CASE("StoreWriteStream (FailingBackend): propagates backend write and read failures")
{
    FailingWriteStreamBackend backend;
    StoreWriteStream<MramLayout, FailingWriteStreamBackend> stream(backend, RecordAccessMode::PayloadOnly);

    SUBCASE("Write failure during output does not advance offset")
    {
        REQUIRE(stream.initialize(makePath("config")) == true);

        const std::uint8_t part1[2] = {1, 2};
        const std::uint8_t part2[2] = {3, 4};

        backend.fail_write = true;
        CHECK(stream.output(part1, sizeof(part1)) == false);

        // Retry after backend recovers; offset is still 0 so full 4 bytes fit
        backend.fail_write = false;
        CHECK(stream.output(part1, sizeof(part1)) == true);
        CHECK(stream.output(part2, sizeof(part2)) == true);
        CHECK(stream.finalize() == true);
    }

    SUBCASE("Read failure during finalize fails commit")
    {
        REQUIRE(stream.initialize(makePath("config")) == true);
        const std::uint8_t full[4] = {1, 2, 3, 4};
        REQUIRE(stream.output(full, sizeof(full)) == true);

        backend.fail_read = true;
        CHECK(stream.finalize() == false);
    }

    SUBCASE("Write failure during finalize fails commit")
    {
        REQUIRE(stream.initialize(makePath("config")) == true);
        const std::uint8_t full[4] = {1, 2, 3, 4};
        REQUIRE(stream.output(full, sizeof(full)) == true);

        backend.fail_write = true;
        CHECK(stream.finalize() == false);
    }
}