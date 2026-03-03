#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Mram.hpp"
#include "MramWriteStream.hpp"

// ------------------------------------------------------------
// Dummy payload types
// ------------------------------------------------------------
struct mram_config_t  { uint8_t a[4]; };
struct mram_state_t   { uint8_t b[8]; };

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
    static constexpr uint32_t offset  = offsetof(MramLayout, config);
    static constexpr uint32_t version = 1;
    static constexpr size_t   index   = 0;
    static constexpr const char* name = "config";
};

template <>
struct MramTraits<mram_state_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, state);
    static constexpr uint32_t version = 1;
    static constexpr size_t   index   = 1;
    static constexpr const char* name = "state";
};

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
// Tests for RAM-backed MramWriteStream
// ------------------------------------------------------------
TEST_CASE("MramWriteStream: initialize finds correct entry")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    MramWriteStream<MramLayout> stream(buffer);

    CHECK(stream.initialize(makePath("config")) == true);
    CHECK(stream.initialize(makePath("state")) == true);
    CHECK(stream.initialize(makePath("does_not_exist")) == false);
}

TEST_CASE("MramWriteStream: output writes raw record bytes at correct offset")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    MramWriteStream<MramLayout> stream(buffer);

    REQUIRE(stream.initialize(makePath("config")) == true);

    uint8_t data[6] = { 1, 2, 3, 4, 5, 6 };
    CHECK(stream.output(data, sizeof(data)) == true);

    uint8_t* base = buffer + MramTraits<mram_config_t, MramLayout>::offset;
    CHECK(base[0] == 1);
    CHECK(base[1] == 2);
    CHECK(base[5] == 6);
}

TEST_CASE("MramWriteStream: output truncates when exceeding record size")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    MramWriteStream<MramLayout> stream(buffer);

    REQUIRE(stream.initialize(makePath("config")) == true);

    std::size_t max_size = sizeof(MramRecord<mram_config_t>);
    uint8_t data[64];
    for (int i = 0; i < 64; i++) data[i] = static_cast<uint8_t>(i);

    CHECK(stream.output(data, sizeof(data)) == true);

    uint8_t* base = buffer + MramTraits<mram_config_t, MramLayout>::offset;
    CHECK(base[max_size - 1] == static_cast<uint8_t>(max_size - 1));
}

TEST_CASE("MramWriteStream: finalize resets state")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    MramWriteStream<MramLayout> stream(buffer);

    REQUIRE(stream.initialize(makePath("config")) == true);
    CHECK(stream.finalize() == true);

    uint8_t data[4] = { 9, 9, 9, 9 };
    CHECK(stream.output(data, sizeof(data)) == false); // no active entry
}

// ------------------------------------------------------------
// Mock transport for chip-backed write stream
// ------------------------------------------------------------
class MockTransport
{
public:
    mutable bool     write_called = false;
    mutable uint32_t last_addr    = 0;
    mutable uint16_t last_len     = 0;
    mutable uint8_t  last_data[64];

    bool writeBytes(uint32_t addr, const void* buf, uint16_t len) const
    {
        write_called = true;
        last_addr = addr;
        last_len  = len;

        const uint8_t* p = static_cast<const uint8_t*>(buf);
        for (uint16_t i = 0; i < len; i++)
            last_data[i] = p[i];

        return true;
    }
};

// ------------------------------------------------------------
// Tests for chip-backed MramChipWriteStream
// ------------------------------------------------------------
TEST_CASE("MramChipWriteStream: initialize finds correct entry")
{
    MockTransport mock;
    MramChipWriteStream<MramLayout, MockTransport> stream(mock);

    CHECK(stream.initialize(makePath("config")) == true);
    CHECK(stream.initialize(makePath("state")) == true);
    CHECK(stream.initialize(makePath("does_not_exist")) == false);
}

TEST_CASE("MramChipWriteStream: output calls transport with correct address")
{
    MockTransport mock;
    MramChipWriteStream<MramLayout, MockTransport> stream(mock);

    REQUIRE(stream.initialize(makePath("state")) == true);

    uint8_t data[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    CHECK(stream.output(data, sizeof(data)) == true);

    CHECK(mock.write_called == true);
    CHECK(mock.last_addr ==
          MramTraits<mram_state_t, MramLayout>::offset);
    CHECK(mock.last_len == 4);
    CHECK(mock.last_data[0] == 0xAA);
    CHECK(mock.last_data[3] == 0xDD);
}

TEST_CASE("MramChipWriteStream: output truncates when exceeding record size")
{
    MockTransport mock;
    MramChipWriteStream<MramLayout, MockTransport> stream(mock);

    REQUIRE(stream.initialize(makePath("config")) == true);

    std::size_t max_size = sizeof(MramRecord<mram_config_t>);
    uint8_t data[64];
    for (int i = 0; i < 64; i++) data[i] = static_cast<uint8_t>(i);

    CHECK(stream.output(data, sizeof(data)) == true);

    CHECK(mock.last_len == max_size);
    CHECK(mock.last_data[max_size - 1] == static_cast<uint8_t>(max_size - 1));
}

TEST_CASE("MramChipWriteStream: finalize resets state")
{
    MockTransport mock;
    MramChipWriteStream<MramLayout, MockTransport> stream(mock);

    REQUIRE(stream.initialize(makePath("config")) == true);
    CHECK(stream.finalize() == true);

    uint8_t data[4] = { 1, 2, 3, 4 };
    CHECK(stream.output(data, sizeof(data)) == false);
}
