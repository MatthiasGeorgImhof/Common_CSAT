#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "FileSystemBackend.hpp"
#include "Mram.hpp"
#include "MramStorageBackend.hpp"
#include "MramStore.hpp"

// ------------------------------------------------------------
// Test layout
// ------------------------------------------------------------
struct FsConfig { std::uint32_t baudrate; std::uint16_t node_id; };
struct FsState  { std::uint8_t  mode;     std::int32_t  counter; };

struct FsLayout
{
    MramRecord<FsConfig> config;
    MramRecord<FsState>  state;
};

template <>
struct MramLayoutMembers<FsLayout>
{
    static constexpr auto members = std::make_tuple(
        &FsLayout::config,
        &FsLayout::state
    );
};

template <>
struct MramTraits<FsConfig, FsLayout>
{
    static constexpr std::uint32_t offset  = offsetof(FsLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "config";
};

template <>
struct MramTraits<FsState, FsLayout>
{
    static constexpr std::uint32_t offset  = offsetof(FsLayout, state);
    static constexpr std::uint32_t version = 2;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "state";
};

// ------------------------------------------------------------
// RAII temp directory helper
// ------------------------------------------------------------
struct TempDir
{
    std::filesystem::path path;

    explicit TempDir(const std::string& tag)
        : path(std::filesystem::temp_directory_path() / ("mram_fs_test_" + tag))
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// ------------------------------------------------------------
// PerRecordFileBackend tests
// ------------------------------------------------------------
TEST_CASE("PerRecordFileBackend: missing file returns false on read")
{
    TempDir tmp("per_rec_missing");
    PerRecordFileBackend backend(tmp.path);

    constexpr auto& cfg_entry = MramRegistry<FsLayout>::entry_for<FsConfig>();
    std::vector<std::uint8_t> buf(cfg_entry.size);

    CHECK(backend.readRecord(cfg_entry, 0, buf.data(), buf.size()) == false);
}

TEST_CASE("PerRecordFileBackend: partial write creates 0xFF-initialized file")
{
    TempDir tmp("per_rec_partial");
    PerRecordFileBackend backend(tmp.path);

    constexpr auto& cfg_entry = MramRegistry<FsLayout>::entry_for<FsConfig>();
    const std::uint8_t slice[4] = {0x11, 0x22, 0x33, 0x44};

    REQUIRE(backend.writeRecord(cfg_entry, 4, slice, sizeof(slice)) == true);

    const auto file_path = tmp.path / "config.bin";
    REQUIRE(std::filesystem::exists(file_path));
    CHECK(std::filesystem::file_size(file_path) == cfg_entry.size);

    std::vector<std::uint8_t> out(cfg_entry.size);
    REQUIRE(backend.readRecord(cfg_entry, 0, out.data(), out.size()) == true);

    // Bytes outside [4, 8) remain erased (0xFF)
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(out[i] == 0xFF);
    CHECK(out[4] == 0x11);
    CHECK(out[5] == 0x22);
    CHECK(out[6] == 0x33);
    CHECK(out[7] == 0x44);
    for (std::size_t i = 8; i < out.size(); ++i)
        CHECK(out[i] == 0xFF);
}

TEST_CASE("PerRecordFileBackend: isolates records and enforces bounds")
{
    TempDir tmp("per_rec_bounds");
    PerRecordFileBackend backend(tmp.path);

    constexpr auto& cfg_entry = MramRegistry<FsLayout>::entry_for<FsConfig>();
    constexpr auto& st_entry  = MramRegistry<FsLayout>::entry_for<FsState>();

    std::vector<std::uint8_t> cfg_data(cfg_entry.size, 0xAA);
    REQUIRE(backend.writeRecord(cfg_entry, 0, cfg_data.data(), cfg_data.size()) == true);

    CHECK(std::filesystem::exists(tmp.path / "config.bin"));
    CHECK(!std::filesystem::exists(tmp.path / "state.bin"));

    std::uint8_t dummy = 0;
    CHECK(backend.readRecord(st_entry, 0, &dummy, 1) == false);

    // Bounds & nullptr checks
    CHECK(backend.writeRecord(cfg_entry, cfg_entry.size, &dummy, 1) == false);
    CHECK(backend.readRecord(cfg_entry, cfg_entry.size, &dummy, 1) == false);
    CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 1) == false);
    CHECK(backend.readRecord(cfg_entry, 0, nullptr, 1) == false);
    CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 0) == true);
    CHECK(backend.readRecord(cfg_entry, 0, nullptr, 0) == true);
}

TEST_CASE("PerRecordFileBackend: persists MramStore records across instances")
{
    TempDir tmp("per_rec_persist");

    {
        PerRecordFileBackend backend(tmp.path);
        MramStore<FsLayout, PerRecordFileBackend> store(backend);

        REQUIRE(store.set(FsConfig{115200, 7}, 0x01) == true);
        REQUIRE(store.set(FsState{3, 42}, 0x02) == true);
    }

    // Reopen in a fresh backend/store instance
    {
        PerRecordFileBackend backend(tmp.path);
        MramStore<FsLayout, PerRecordFileBackend> store(backend);

        FsConfig cfg{};
        MramRecord<FsState> st_rec{};
        REQUIRE(store.get(cfg) == true);
        REQUIRE(store.get_record(st_rec) == true);

        CHECK(cfg.baudrate == 115200);
        CHECK(cfg.node_id  == 7);
        CHECK(st_rec.seq   == 1);
        CHECK(st_rec.flags == 0x02);
        CHECK(st_rec.payload.mode == 3);
        CHECK(st_rec.payload.counter == 42);

        // Next write continues sequence numbering
        REQUIRE(store.set(FsState{4, 99}) == true);
        REQUIRE(store.get_record(st_rec) == true);
        CHECK(st_rec.seq == 2);
    }
}

// ------------------------------------------------------------
// FlatFileBackend tests
// ------------------------------------------------------------
TEST_CASE("FlatFileBackend: initializes image with 0xFF and creates parent dirs")
{
    TempDir tmp("flat_init");
    const auto image = tmp.path / "nested" / "mram.bin";

    FlatFileBackend backend(image, sizeof(FsLayout));

    REQUIRE(std::filesystem::exists(image));
    CHECK(std::filesystem::file_size(image) == sizeof(FsLayout));

    constexpr auto& cfg_entry = MramRegistry<FsLayout>::entry_for<FsConfig>();
    std::vector<std::uint8_t> out(cfg_entry.size, 0);
    REQUIRE(backend.readRecord(cfg_entry, 0, out.data(), out.size()) == true);

    for (auto b : out)
        CHECK(b == 0xFF);
}

TEST_CASE("FlatFileBackend: read/write round-trip, isolation, and bounds checks")
{
    TempDir tmp("flat_rw");
    const auto image = tmp.path / "mram.bin";

    FlatFileBackend backend(image, sizeof(FsLayout));
    constexpr auto& cfg_entry = MramRegistry<FsLayout>::entry_for<FsConfig>();
    constexpr auto& st_entry  = MramRegistry<FsLayout>::entry_for<FsState>();

    std::vector<std::uint8_t> in(cfg_entry.size);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<std::uint8_t>(0x10 + i);

    REQUIRE(backend.writeRecord(cfg_entry, 0, in.data(), in.size()) == true);

    // Partial read
    std::uint8_t part[4]{};
    REQUIRE(backend.readRecord(cfg_entry, 3, part, sizeof(part)) == true);
    CHECK(part[0] == 0x13);
    CHECK(part[3] == 0x16);

    // Adjacent record remains 0xFF
    std::vector<std::uint8_t> st_bytes(st_entry.size);
    REQUIRE(backend.readRecord(st_entry, 0, st_bytes.data(), st_bytes.size()) == true);
    for (auto b : st_bytes)
        CHECK(b == 0xFF);

    // Bounds & nullptr checks
    std::uint8_t b = 0;
    CHECK(backend.writeRecord(cfg_entry, cfg_entry.size, &b, 1) == false);
    CHECK(backend.readRecord(cfg_entry, cfg_entry.size, &b, 1) == false);
    CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 1) == false);
    CHECK(backend.readRecord(cfg_entry, 0, nullptr, 1) == false);
    CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 0) == true);
    CHECK(backend.readRecord(cfg_entry, 0, nullptr, 0) == true);
}

TEST_CASE("FlatFileBackend: rejects image smaller than layout")
{
    TempDir tmp("flat_small");
    const auto image = tmp.path / "tiny.bin";

    FlatFileBackend backend(image, sizeof(MramRecord<FsConfig>) + 2);
    constexpr auto& cfg_entry = MramRegistry<FsLayout>::entry_for<FsConfig>();
    constexpr auto& st_entry  = MramRegistry<FsLayout>::entry_for<FsState>();

    std::vector<std::uint8_t> tmp_buf(st_entry.size, 0x55);
    CHECK(backend.writeRecord(cfg_entry, 0, tmp_buf.data(), cfg_entry.size) == true);
    CHECK(backend.writeRecord(st_entry, 0, tmp_buf.data(), st_entry.size) == false);
    CHECK(backend.readRecord(st_entry, 0, tmp_buf.data(), st_entry.size) == false);
}

TEST_CASE("FlatFileBackend: persists MramStore records across instances")
{
    TempDir tmp("flat_persist");
    const auto image = tmp.path / "mram.bin";

    {
        FlatFileBackend backend(image, sizeof(FsLayout));
        MramStore<FsLayout, FlatFileBackend> store(backend);

        REQUIRE(store.set(FsConfig{250000, 15}) == true);
        REQUIRE(store.set(FsConfig{500000, 16}) == true);
    }

    {
        FlatFileBackend backend(image, sizeof(FsLayout));
        MramStore<FsLayout, FlatFileBackend> store(backend);

        MramRecord<FsConfig> rec{};
        REQUIRE(store.get_record(rec) == true);
        CHECK(rec.seq == 2);
        CHECK(rec.payload.baudrate == 500000);
        CHECK(rec.payload.node_id  == 16);
    }
}