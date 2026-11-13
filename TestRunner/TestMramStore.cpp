#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

#include "Mram.hpp"
#include "MramStorageBackend.hpp"
#include "MramStore.hpp"
#include "RamMramMock.hpp"

// ------------------------------------------------------------
// Test layout & traits
// ------------------------------------------------------------
struct mram_config_t
{
    std::uint32_t baudrate;
    std::uint16_t node_id;
};

struct mram_state_t
{
    std::uint8_t mode;
    std::int32_t counter;
};

struct MramLayout
{
    MramRecord<mram_config_t> config;
    MramRecord<mram_state_t>  state;
};

template <>
struct MramLayoutMembers<MramLayout>
{
    static constexpr auto members = std::make_tuple(
        &MramLayout::config,
        &MramLayout::state
    );
};

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
    static constexpr std::uint32_t version = 3;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "state";
};

// ------------------------------------------------------------
// Test backends
// ------------------------------------------------------------
template <std::size_t Size>
class RamMockBackend
{
public:
    explicit RamMockBackend(RamMramMock<Size>& mock) : mock_(mock) {}

    bool readRecord(const MramRegistryEntry& entry,
                    std::size_t offset,
                    std::uint8_t* dst,
                    std::size_t len) const
    {
        if (!mram_validate_range(entry, offset, len))
            return false;

        std::size_t done = 0;
        while (done < len)
        {
            const auto chunk = static_cast<std::uint16_t>(
                std::min<std::size_t>(len - done, std::numeric_limits<std::uint16_t>::max()));
            const auto addr = static_cast<std::uint32_t>(entry.offset + offset + done);
            if (!mock_.readBytes(addr, dst + done, chunk))
                return false;
            done += chunk;
        }
        return true;
    }

    bool writeRecord(const MramRegistryEntry& entry,
                     std::size_t offset,
                     const std::uint8_t* src,
                     std::size_t len)
    {
        if (!mram_validate_range(entry, offset, len))
            return false;

        std::size_t done = 0;
        while (done < len)
        {
            const auto chunk = static_cast<std::uint16_t>(
                std::min<std::size_t>(len - done, std::numeric_limits<std::uint16_t>::max()));
            const auto addr = static_cast<std::uint32_t>(entry.offset + offset + done);
            if (!mock_.writeBytes(addr, src + done, chunk))
                return false;
            done += chunk;
        }
        return true;
    }

private:
    RamMramMock<Size>& mock_;
};

class FailingBackend
{
public:
    bool fail_read  = false;
    bool fail_write = false;

    bool readRecord(const MramRegistryEntry&, std::size_t, std::uint8_t*, std::size_t) const
    {
        return !fail_read;
    }

    bool writeRecord(const MramRegistryEntry&, std::size_t, const std::uint8_t*, std::size_t)
    {
        return !fail_write;
    }
};

// Negative concept test fixtures
struct BadBackendNonConstRead
{
    bool readRecord(const MramRegistryEntry&, std::size_t, std::uint8_t*, std::size_t); // not const
    bool writeRecord(const MramRegistryEntry&, std::size_t, const std::uint8_t*, std::size_t);
};

struct BadBackendWrongReturn
{
    void readRecord(const MramRegistryEntry&, std::size_t, std::uint8_t*, std::size_t) const;
    bool writeRecord(const MramRegistryEntry&, std::size_t, const std::uint8_t*, std::size_t);
};

struct BadBackendMissingWrite
{
    bool readRecord(const MramRegistryEntry&, std::size_t, std::uint8_t*, std::size_t) const;
};

static_assert(MramStorageBackend<RamMockBackend<sizeof(MramLayout)>>);
static_assert(MramStorageBackend<FailingBackend>);
static_assert(!MramStorageBackend<BadBackendNonConstRead>);
static_assert(!MramStorageBackend<BadBackendWrongReturn>);
static_assert(!MramStorageBackend<BadBackendMissingWrite>);

// ------------------------------------------------------------
// MramStorageBackend tests
// ------------------------------------------------------------
TEST_CASE("mram_validate_range rejects out-of-bounds and overflow")
{
    constexpr auto& entry = MramRegistry<MramLayout>::entry_for<mram_config_t>();

    CHECK(mram_validate_range(entry, 0, entry.size) == true);
    CHECK(mram_validate_range(entry, entry.size, 0) == true);
    CHECK(mram_validate_range(entry, 4, entry.size - 4) == true);

    CHECK(mram_validate_range(entry, entry.size + 1, 0) == false);
    CHECK(mram_validate_range(entry, 1, entry.size) == false);
    CHECK(mram_validate_range(entry, 8, std::numeric_limits<std::size_t>::max()) == false);
}

TEST_CASE("RamMockBackend enforces record bounds on read and write")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);

    constexpr auto& entry = MramRegistry<MramLayout>::entry_for<mram_config_t>();
    std::array<std::uint8_t, 64> buf{};

    CHECK(backend.writeRecord(entry, entry.size - 1, buf.data(), 2) == false);
    CHECK(backend.readRecord(entry, entry.size - 1, buf.data(), 2) == false);
    CHECK(backend.writeRecord(entry, 0, buf.data(), 0) == true);
    CHECK(backend.readRecord(entry, 0, buf.data(), 0) == true);
}

// ------------------------------------------------------------
// MramStore tests
// ------------------------------------------------------------
TEST_CASE("MramStore: erased MRAM fails get and preserves output parameter")
{
    RamMramMock<sizeof(MramLayout)> mock; // filled with 0xFF
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    mram_config_t cfg{999, 77};
    CHECK(store.get(cfg) == false);
    CHECK(cfg.baudrate == 999);
    CHECK(cfg.node_id == 77);
}

TEST_CASE("MramStore: set and get round-trip payload and metadata")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    const mram_config_t expected{115200, 42};
    REQUIRE(store.set(expected, 0xA5A50001u) == true);

    mram_config_t actual{};
    REQUIRE(store.get(actual) == true);
    CHECK(actual.baudrate == 115200);
    CHECK(actual.node_id == 42);

    MramRecord<mram_config_t> rec{};
    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.version == MramTraits<mram_config_t, MramLayout>::version);
    CHECK(rec.seq     == 1);
    CHECK(rec.flags   == 0xA5A50001u);
    CHECK(rec.crc     != 0);
}

TEST_CASE("MramStore: repeated set increments sequence number")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    REQUIRE(store.set(mram_state_t{1, 10}) == true);
    MramRecord<mram_state_t> rec{};
    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.seq == 1);

    REQUIRE(store.set(mram_state_t{2, 20}) == true);
    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.seq == 2);

    REQUIRE(store.set(mram_state_t{3, 30}) == true);
    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.seq == 3);
    CHECK(rec.payload.mode == 3);
    CHECK(rec.payload.counter == 30);
}

TEST_CASE("MramStore: records in layout are isolated from each other")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    REQUIRE(store.set(mram_config_t{500000, 12}) == true);
    REQUIRE(store.set(mram_state_t{7, -1234}) == true);

    mram_config_t cfg{};
    mram_state_t  st{};
    REQUIRE(store.get(cfg) == true);
    REQUIRE(store.get(st) == true);

    CHECK(cfg.baudrate == 500000);
    CHECK(cfg.node_id  == 12);
    CHECK(st.mode      == 7);
    CHECK(st.counter   == -1234);
}

TEST_CASE("MramStore: detects corruption in payload, header fields, and CRC")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    constexpr auto base = MramTraits<mram_config_t, MramLayout>::offset;

    SUBCASE("Corrupted payload byte")
    {
        REQUIRE(store.set(mram_config_t{1000, 5}) == true);
        mock.data()[base + offsetof(MramRecord<mram_config_t>, payload)] ^= 0x01;

        mram_config_t out{};
        CHECK(store.get(out) == false);
    }

    SUBCASE("Corrupted seq field")
    {
        REQUIRE(store.set(mram_config_t{1000, 5}) == true);
        mock.data()[base + offsetof(MramRecord<mram_config_t>, seq)] ^= 0x80;

        mram_config_t out{};
        CHECK(store.get(out) == false);
    }

    SUBCASE("Corrupted flags field")
    {
        REQUIRE(store.set(mram_config_t{1000, 5}, 0x1234) == true);
        mock.data()[base + offsetof(MramRecord<mram_config_t>, flags)] ^= 0x04;

        mram_config_t out{};
        CHECK(store.get(out) == false);
    }

    SUBCASE("Corrupted crc field")
    {
        REQUIRE(store.set(mram_config_t{1000, 5}) == true);
        mock.data()[base + offsetof(MramRecord<mram_config_t>, crc)] ^= 0x01;

        mram_config_t out{};
        CHECK(store.get(out) == false);
    }
}

TEST_CASE("MramStore: detects version mismatch and recovers on next set")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    REQUIRE(store.set(mram_state_t{1, 100}) == true);
    REQUIRE(store.set(mram_state_t{2, 200}) == true);

    // Overwrite version + recompute CRC with wrong version to isolate version check
    MramRecord<mram_state_t> raw{};
    constexpr auto offset = MramTraits<mram_state_t, MramLayout>::offset;
    std::memcpy(&raw, mock.data() + offset, sizeof(raw));
    raw.version = 99;
    raw.crc = detail::compute_record_crc32(raw);
    std::memcpy(mock.data() + offset, &raw, sizeof(raw));

    mram_state_t out{9, 9};
    CHECK(store.get(out) == false);
    CHECK(out.mode == 9); // unchanged

    // Next set resets seq to 1 and restores expected version
    REQUIRE(store.set(mram_state_t{4, 400}) == true);
    MramRecord<mram_state_t> recovered{};
    REQUIRE(store.get_record(recovered) == true);
    CHECK(recovered.version == MramTraits<mram_state_t, MramLayout>::version);
    CHECK(recovered.seq     == 1);
    CHECK(recovered.payload.counter == 400);
}

TEST_CASE("MramStore: propagates backend read and write failures")
{
    FailingBackend backend;
    MramStore<MramLayout, FailingBackend> store(backend);

    backend.fail_write = true;
    CHECK(store.set(mram_config_t{9600, 1}) == false);

    backend.fail_read = true;
    mram_config_t out{};
    CHECK(store.get(out) == false);
}

// ------------------------------------------------------------
// MramStore raw (runtime MramRegistryEntry) tests
// ------------------------------------------------------------
TEST_CASE("MramStore: get_raw and set_raw reject null pointers and erased MRAM")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    constexpr auto& cfg_entry = MramRegistry<MramLayout>::entry_for<mram_config_t>();

    CHECK(store.get_raw(cfg_entry, nullptr) == false);
    CHECK(store.set_raw(cfg_entry, nullptr) == false);

    mram_config_t cfg{777, 19};
    CHECK(store.get_raw(cfg_entry, &cfg) == false);
    CHECK(cfg.baudrate == 777);
    CHECK(cfg.node_id  == 19);
}

TEST_CASE("MramStore: set_raw and get_raw round-trip and interoperate with typed set/get")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    constexpr auto& cfg_entry = MramRegistry<MramLayout>::entry_for<mram_config_t>();

    // 1. Write via set_raw, read via get_raw and typed get/get_record
    const mram_config_t cfg1{115200, 10};
    REQUIRE(store.set_raw(cfg_entry, &cfg1, 0xCAFE0001u) == true);

    mram_config_t raw_out{};
    REQUIRE(store.get_raw(cfg_entry, &raw_out) == true);
    CHECK(raw_out.baudrate == 115200);
    CHECK(raw_out.node_id  == 10);

    MramRecord<mram_config_t> rec{};
    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.version == MramTraits<mram_config_t, MramLayout>::version);
    CHECK(rec.seq     == 1);
    CHECK(rec.flags   == 0xCAFE0001u);

    // 2. Write via typed set, read via get_raw (seq continues 1 -> 2)
    const mram_config_t cfg2{500000, 11};
    REQUIRE(store.set(cfg2, 0x0002u) == true);

    REQUIRE(store.get_raw(cfg_entry, &raw_out) == true);
    CHECK(raw_out.baudrate == 500000);
    CHECK(raw_out.node_id  == 11);

    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.seq == 2);

    // 3. Write again via set_raw (seq continues 2 -> 3)
    const mram_config_t cfg3{1000000, 12};
    REQUIRE(store.set_raw(cfg_entry, &cfg3, 0x0003u) == true);
    REQUIRE(store.get_record(rec) == true);
    CHECK(rec.seq   == 3);
    CHECK(rec.flags == 0x0003u);
    CHECK(rec.payload.baudrate == 1000000);
    CHECK(rec.payload.node_id  == 12);
}

TEST_CASE("MramStore: get_raw detects corruption and version mismatch; set_raw recovers")
{
    RamMramMock<sizeof(MramLayout)> mock;
    RamMockBackend backend(mock);
    MramStore<MramLayout, decltype(backend)> store(backend);

    constexpr auto& st_entry = MramRegistry<MramLayout>::entry_for<mram_state_t>();
    constexpr auto  base     = MramTraits<mram_state_t, MramLayout>::offset;

    SUBCASE("Corrupted payload")
    {
        auto entry = mram_state_t{5, 42};
        REQUIRE(store.set_raw(st_entry, &entry) == true);
        mock.data()[base + offsetof(MramRecord<mram_state_t>, payload)] ^= 0x01;

        mram_state_t out{};
        CHECK(store.get_raw(st_entry, &out) == false);
    }

    SUBCASE("Corrupted header crc")
    {
        auto entry = mram_state_t{5, 42};
        REQUIRE(store.set_raw(st_entry, &entry) == true);
        mock.data()[base + offsetof(MramRecord<mram_state_t>, crc)] ^= 0x80;

        mram_state_t out{};
        CHECK(store.get_raw(st_entry, &out) == false);
    }

    SUBCASE("Version mismatch resets seq on next set_raw")
    {
        auto entry1 = mram_state_t{1, 10};
        auto entry2 = mram_state_t{2, 20};
        REQUIRE(store.set_raw(st_entry, &entry1) == true);
        REQUIRE(store.set_raw(st_entry, &entry2) == true);

        MramRecord<mram_state_t> raw{};
        std::memcpy(&raw, mock.data() + base, sizeof(raw));
        raw.version = 99;
        raw.crc = detail::compute_record_crc32(raw);
        std::memcpy(mock.data() + base, &raw, sizeof(raw));

        mram_state_t out{9, 9};
        CHECK(store.get_raw(st_entry, &out) == false);
        CHECK(out.mode == 9);

        auto entry3 = mram_state_t{3, 30};
        REQUIRE(store.set_raw(st_entry, &entry3) == true);
        MramRecord<mram_state_t> recovered{};
        REQUIRE(store.get_record(recovered) == true);
        CHECK(recovered.version == MramTraits<mram_state_t, MramLayout>::version);
        CHECK(recovered.seq     == 1);
        CHECK(recovered.payload.counter == 30);
    }
}

TEST_CASE("MramStore: get_raw and set_raw propagate backend failures")
{
    FailingBackend backend;
    MramStore<MramLayout, FailingBackend> store(backend);
    constexpr auto& cfg_entry = MramRegistry<MramLayout>::entry_for<mram_config_t>();

    const mram_config_t cfg{9600, 1};
    backend.fail_write = true;
    CHECK(store.set_raw(cfg_entry, &cfg) == false);

    backend.fail_read = true;
    mram_config_t out{};
    CHECK(store.get_raw(cfg_entry, &out) == false);
}