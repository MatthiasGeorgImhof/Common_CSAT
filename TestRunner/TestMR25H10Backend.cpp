#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "MR25H10.hpp"
#include "MR25H10Backend.hpp"
#include "RecordRegistry.hpp"
#include "StorageBackend.hpp"
#include "RecordStore.hpp"
#include "Transport.hpp"
#include "mock_hal.h"

// ------------------------------------------------------------
// Transport / MR25H10 test fixture (same setup as TestMR25H10)
// ------------------------------------------------------------
SPI_HandleTypeDef mock_spi;
GPIO_TypeDef mock_gpio = {};
static constexpr uint32_t MOCK_GPIO_BASE = static_cast<uint32_t>(0xDEADBEEF);
auto constexpr CS_PIN = GPIO_PIN_5;

using CS_Pin    = GpioPin<MOCK_GPIO_BASE, CS_PIN>;
using Config    = SPI_Stream_Config<mock_spi, CS_Pin, 128>;
using Transport = SPIStreamTransport<Config>;
using SRAM      = MR25H10<Transport>;

static_assert(StorageBackend<MR25H10Backend<Transport>>);
static_assert(StorageBackend<MR25H10Backend<Transport, 8, 128 * 1024>>);

// ------------------------------------------------------------
// Test layout
// ------------------------------------------------------------
struct ChipConfig { std::uint32_t baudrate; };
struct ChipState  { std::uint32_t counter;  };

struct ChipLayout
{
    StorageRecord<ChipConfig> config; // 20 bytes at offset 0
    StorageRecord<ChipState>  state;  // 20 bytes at offset 20
};

template <>
struct LayoutMembers<ChipLayout>
{
    static constexpr auto members = std::make_tuple(
        &ChipLayout::config,
        &ChipLayout::state
    );
};

template <>
struct RecordTraits<ChipConfig, ChipLayout>
{
    static constexpr std::uint32_t offset  = offsetof(ChipLayout, config);
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t   index   = 0;
    static constexpr const char*   name    = "config";
};

template <>
struct RecordTraits<ChipState, ChipLayout>
{
    static constexpr std::uint32_t offset  = offsetof(ChipLayout, state);
    static constexpr std::uint32_t version = 2;
    static constexpr std::size_t   index   = 1;
    static constexpr const char*   name    = "state";
};

// ------------------------------------------------------------
// Tests
// ------------------------------------------------------------
TEST_CASE("MR25H10Backend: single-chunk readRecord sends 24-bit address and reads data")
{
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();

    Config config;
    Transport transport(config);
    SRAM sram(transport);
    MR25H10Backend<Transport, 64> backend(sram);

    constexpr auto& st_entry = RecordRegistry<ChipLayout>::entry_for<ChipState>();
    std::uint8_t expected[4] = {0x12, 0x34, 0x56, 0x78};
    inject_spi_rx_data(expected, sizeof(expected));

    std::uint8_t out[4]{};
    REQUIRE(backend.readRecord(st_entry, 6, out, sizeof(out)) == true);

    const auto* tx = get_spi_tx_buffer();
    const std::uint32_t expected_addr = st_entry.offset + 6;
    CHECK(tx[0] == static_cast<std::uint8_t>(MR25H10_COMMANDS::MR25H10_READ));
    CHECK(tx[1] == static_cast<std::uint8_t>((expected_addr >> 16) & 0xFF));
    CHECK(tx[2] == static_cast<std::uint8_t>((expected_addr >> 8) & 0xFF));
    CHECK(tx[3] == static_cast<std::uint8_t>(expected_addr & 0xFF));

    CHECK(std::memcmp(out, expected, sizeof(expected)) == 0);
}

TEST_CASE("MR25H10Backend: multi-chunk writeRecord splits transfers by MaxChunkSize")
{
    clear_spi_tx_buffer();

    Config config;
    Transport transport(config);
    SRAM sram(transport);
    MR25H10Backend<Transport, 8> backend(sram); // 8-byte chunks

    constexpr auto& st_entry = RecordRegistry<ChipLayout>::entry_for<ChipState>();
    REQUIRE(st_entry.size == 20); // 8 + 8 + 4 -> 3 chunks

    std::vector<std::uint8_t> payload(st_entry.size);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::uint8_t>(0xA0 + i);

    REQUIRE(backend.writeRecord(st_entry, 0, payload.data(), payload.size()) == true);

    const auto* tx = get_spi_tx_buffer();
    // Each chunk emits: WREN (1) + WRITE + 3-byte addr (4) + chunk bytes
    CHECK(get_spi_tx_buffer_count() == (3 * 5) + 20);

    // Chunk 0: 8 bytes at st_entry.offset + 0
    const std::uint32_t addr0 = st_entry.offset;
    CHECK(tx[0] == static_cast<std::uint8_t>(MR25H10_COMMANDS::MR25H10_WREN));
    CHECK(tx[1] == static_cast<std::uint8_t>(MR25H10_COMMANDS::MR25H10_WRITE));
    CHECK(tx[2] == static_cast<std::uint8_t>((addr0 >> 16) & 0xFF));
    CHECK(tx[3] == static_cast<std::uint8_t>((addr0 >> 8) & 0xFF));
    CHECK(tx[4] == static_cast<std::uint8_t>(addr0 & 0xFF));
    CHECK(std::memcmp(&tx[5], &payload[0], 8) == 0);

    // Chunk 1: 8 bytes at st_entry.offset + 8
    const std::size_t off1 = 5 + 8;
    const std::uint32_t addr1 = st_entry.offset + 8;
    CHECK(tx[off1 + 0] == static_cast<std::uint8_t>(MR25H10_COMMANDS::MR25H10_WREN));
    CHECK(tx[off1 + 1] == static_cast<std::uint8_t>(MR25H10_COMMANDS::MR25H10_WRITE));
    CHECK(tx[off1 + 4] == static_cast<std::uint8_t>(addr1 & 0xFF));
    CHECK(std::memcmp(&tx[off1 + 5], &payload[8], 8) == 0);

    // Chunk 2: 4 bytes at st_entry.offset + 16
    const std::size_t off2 = off1 + 5 + 8;
    const std::uint32_t addr2 = st_entry.offset + 16;
    CHECK(tx[off2 + 0] == static_cast<std::uint8_t>(MR25H10_COMMANDS::MR25H10_WREN));
    CHECK(tx[off2 + 1] == static_cast<std::uint8_t>(MR25H10_COMMANDS::MR25H10_WRITE));
    CHECK(tx[off2 + 4] == static_cast<std::uint8_t>(addr2 & 0xFF));
    CHECK(std::memcmp(&tx[off2 + 5], &payload[16], 4) == 0);
}

TEST_CASE("MR25H10Backend: multi-chunk readRecord reassembles payload")
{
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();

    Config config;
    Transport transport(config);
    SRAM sram(transport);
    MR25H10Backend<Transport, 8> backend(sram);

    constexpr auto& cfg_entry = RecordRegistry<ChipLayout>::entry_for<ChipConfig>();
    std::vector<std::uint8_t> expected(cfg_entry.size);
    for (std::size_t i = 0; i < expected.size(); ++i)
        expected[i] = static_cast<std::uint8_t>(0x30 + i);

    inject_spi_rx_data(expected.data(), static_cast<uint16_t>(expected.size()));

    std::vector<std::uint8_t> actual(cfg_entry.size, 0);
    REQUIRE(backend.readRecord(cfg_entry, 0, actual.data(), actual.size()) == true);
    CHECK(actual == expected);
}

TEST_CASE("MR25H10Backend: rejects invalid bounds, capacity, and null pointers without SPI traffic")
{
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();

    Config config;
    Transport transport(config);
    SRAM sram(transport);

    constexpr auto& cfg_entry = RecordRegistry<ChipLayout>::entry_for<ChipConfig>();
    constexpr auto& st_entry  = RecordRegistry<ChipLayout>::entry_for<ChipState>();

    SUBCASE("Record bounds and null pointers")
    {
        MR25H10Backend<Transport, 64> backend(sram);
        std::uint8_t buf[32]{};

        CHECK(backend.writeRecord(cfg_entry, cfg_entry.size, buf, 1) == false);
        CHECK(backend.readRecord(cfg_entry, cfg_entry.size, buf, 1) == false);
        CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 1) == false);
        CHECK(backend.readRecord(cfg_entry, 0, nullptr, 1) == false);

        // Zero-length succeeds without SPI transactions
        CHECK(backend.writeRecord(cfg_entry, 0, nullptr, 0) == true);
        CHECK(backend.readRecord(cfg_entry, 0, nullptr, 0) == true);
        CHECK(get_spi_tx_buffer_count() == 0);
    }

    SUBCASE("ChipCapacity bounds")
    {
        MR25H10Backend<Transport, 64, sizeof(StorageRecord<ChipConfig>) + 4> small_chip(sram);
        std::uint8_t buf[32]{};

        CHECK(small_chip.writeRecord(st_entry, 0, buf, st_entry.size) == false);
        CHECK(small_chip.readRecord(st_entry, 0, buf, st_entry.size) == false);
        CHECK(get_spi_tx_buffer_count() == 0);
    }
}

TEST_CASE("MR25H10Backend: integrates with RecordStore get and set")
{
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();

    Config config;
    Transport transport(config);
    SRAM sram(transport);
    MR25H10Backend<Transport, 64> backend(sram);
    RecordStore<ChipLayout, decltype(backend)> store(backend);

    // Prepare a valid stored record for get_record() during set()'s seq lookup
    StorageRecord<ChipConfig> existing{};
    existing.version = RecordTraits<ChipConfig, ChipLayout>::version;
    existing.seq     = 4;
    existing.flags   = 0;
    existing.payload = ChipConfig{9600};
    existing.crc     = detail::compute_record_crc32(existing);

    // Inject existing record for store.get()
    inject_spi_rx_data(reinterpret_cast<std::uint8_t*>(&existing), sizeof(existing));
    ChipConfig read_cfg{};
    REQUIRE(store.get(read_cfg) == true);
    CHECK(read_cfg.baudrate == 9600);

    // Inject existing record for store.set()'s internal seq read, then verify written record
    clear_spi_tx_buffer();
    clear_spi_rx_buffer();
    inject_spi_rx_data(reinterpret_cast<std::uint8_t*>(&existing), sizeof(existing));

    REQUIRE(store.set(ChipConfig{115200}, 0x55) == true);

    // TX buffer holds: READ header (4) + WREN (1) + WRITE header (4) + StorageRecord (20)
    const auto* tx = get_spi_tx_buffer();
    StorageRecord<ChipConfig> written{};
    // Last sizeof(StorageRecord<ChipConfig>) bytes in TX are the written record
    const std::size_t count = get_spi_tx_buffer_count();
    REQUIRE(count >= sizeof(written));
    std::memcpy(&written, &tx[count - sizeof(written)], sizeof(written));

    CHECK(written.version == 1);
    CHECK(written.seq     == 5);
    CHECK(written.flags   == 0x55);
    CHECK(written.payload.baudrate == 115200);
    CHECK(written.crc == detail::compute_record_crc32(written));
}