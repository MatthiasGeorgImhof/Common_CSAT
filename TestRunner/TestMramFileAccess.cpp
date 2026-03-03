#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Mram.hpp"
#include "MramFileAccess.hpp"

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
// Tests for RAM-backed MramFileAccess
// ------------------------------------------------------------
TEST_CASE("MramFileAccess: full read of config record (raw record bytes)")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    auto* layout = reinterpret_cast<MramLayout*>(buffer);

    // Fill entire record (header + payload) with a known pattern
    auto* rec_bytes = reinterpret_cast<uint8_t*>(&layout->config);
    for (std::size_t i = 0; i < sizeof(MramRecord<mram_config_t>); ++i)
        rec_bytes[i] = static_cast<uint8_t>(0x10 + i);

    MramFileAccess access(buffer);

    std::uint8_t out[32]{};
    std::size_t size = sizeof(out);

    bool ok = access.read<MramLayout>(makePath("config"), 0, out, size);

    CHECK(ok == true);
    CHECK(size == sizeof(MramRecord<mram_config_t>));
    CHECK(out[0] == 0x10);
    CHECK(out[1] == 0x11);
    CHECK(out[sizeof(MramRecord<mram_config_t>) - 1] ==
          static_cast<uint8_t>(0x10 + sizeof(MramRecord<mram_config_t>) - 1));
}

TEST_CASE("MramFileAccess: partial read with offset inside record")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    auto* layout = reinterpret_cast<MramLayout*>(buffer);

    auto* rec_bytes = reinterpret_cast<uint8_t*>(&layout->state);
    for (std::size_t i = 0; i < sizeof(MramRecord<mram_state_t>); ++i)
        rec_bytes[i] = static_cast<uint8_t>(0x20 + i);

    MramFileAccess access(buffer);

    std::uint8_t out[4]{};
    std::size_t size = sizeof(out);

    std::size_t offset = 5; // somewhere in the record
    bool ok = access.read<MramLayout>(makePath("state"), offset, out, size);

    CHECK(ok == true);
    CHECK(size == 4);
    CHECK(out[0] == static_cast<uint8_t>(0x20 + offset));
    CHECK(out[1] == static_cast<uint8_t>(0x20 + offset + 1));
}

TEST_CASE("MramFileAccess: offset beyond end returns EOF")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    MramFileAccess access(buffer);

    std::uint8_t out[4]{};
    std::size_t size = sizeof(out);

    bool ok = access.read<MramLayout>(makePath("config"),
                                      sizeof(MramRecord<mram_config_t>) + 10,
                                      out, size);

    CHECK(ok == true);   // EOF is not an error
    CHECK(size == 0);
}

TEST_CASE("MramFileAccess: missing file returns false")
{
    std::uint8_t buffer[sizeof(MramLayout)]{};
    MramFileAccess access(buffer);

    std::uint8_t out[4]{};
    std::size_t size = sizeof(out);

    bool ok = access.read<MramLayout>(makePath("does_not_exist"), 0, out, size);

    CHECK(ok == false);
    CHECK(size == 0);
}

// ------------------------------------------------------------
// Mock transport for chip-backed access
// ------------------------------------------------------------
class MockTransport
{
public:
    mutable bool     readBytes_called = false;
    mutable uint32_t last_addr        = 0;
    mutable uint16_t last_len         = 0;

    bool readBytes(uint32_t addr, void* buf, uint16_t len) const
    {
        readBytes_called = true;
        last_addr = addr;
        last_len  = len;

        auto* p = static_cast<uint8_t*>(buf);
        for (uint16_t i = 0; i < len; ++i)
            p[i] = 0xCC;

        return true;
    }
};

// ------------------------------------------------------------
// Tests for chip-backed MramChipFileAccess
// ------------------------------------------------------------
TEST_CASE("MramChipFileAccess: read calls transport with correct address")
{
    MockTransport mock;
    MramChipFileAccess<MramLayout, MockTransport> access(mock);

    std::uint8_t out[8]{};
    std::size_t size = sizeof(out);

    bool ok = access.read(makePath("state"), 4, out, size);

    CHECK(ok == true);
    CHECK(mock.readBytes_called == true);

    CHECK(mock.last_addr ==
          MramTraits<mram_state_t, MramLayout>::offset + 4);
    CHECK(mock.last_len == 8);
    CHECK(out[0] == 0xCC);
}
