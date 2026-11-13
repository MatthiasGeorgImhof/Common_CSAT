#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Mram.hpp"
#include <type_traits>

// ------------------------------------------------------------
// Dummy payload types for testing
// ------------------------------------------------------------
struct mram_config_t  { int a; };
struct mram_command_t { int b; };
struct mram_state_t   { int c; };
struct mram_log_t     { int d; };
struct mram_unused_t  { int e; };

// ------------------------------------------------------------
// Mission-specific layout
// ------------------------------------------------------------
struct MramLayout
{
    MramRecord<mram_config_t>   config;
    MramRecord<mram_command_t>  command;
    MramRecord<mram_state_t>    state;
    MramRecord<mram_log_t>      log;
};

// ------------------------------------------------------------
// Mission-specific member list
// ------------------------------------------------------------
template <>
struct MramLayoutMembers<MramLayout>
{
    static constexpr auto members = std::make_tuple(
        &MramLayout::config,
        &MramLayout::command,
        &MramLayout::state,
        &MramLayout::log
    );
};

// ------------------------------------------------------------
// Mission-specific traits
// ------------------------------------------------------------
template <>
struct MramTraits<mram_config_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, config);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 0;
    static constexpr const char* name = "config";
};

template <>
struct MramTraits<mram_command_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, command);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 1;
    static constexpr const char* name = "command";
};

template <>
struct MramTraits<mram_state_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, state);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 2;
    static constexpr const char* name = "state";
};

template <>
struct MramTraits<mram_log_t, MramLayout>
{
    static constexpr uint32_t offset  = offsetof(MramLayout, log);
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 3;
    static constexpr const char* name = "log";
};

// ------------------------------------------------------------
// Compile-time checks (concept, traits, constexpr lookups)
// ------------------------------------------------------------
struct InvalidTraitsMissingName
{
    static constexpr uint32_t offset  = 0;
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 0;
};

struct InvalidTraitsBadNameType
{
    static constexpr uint32_t offset  = 0;
    static constexpr uint32_t version = 1;
    static constexpr std::size_t index = 0;
    static constexpr int name = 42;
};

static_assert(MramTraitsConcept<MramTraits<mram_config_t, MramLayout>>);
static_assert(!MramTraitsConcept<InvalidTraitsMissingName>);
static_assert(!MramTraitsConcept<InvalidTraitsBadNameType>);

static_assert(std::is_same_v<payload_of<MramRecord<mram_config_t>>::type, mram_config_t>);
static_assert(std::is_same_v<member_payload<MramLayout, decltype(&MramLayout::state)>::type, mram_state_t>);

static_assert(compute_member_index<MramLayout, mram_config_t>() == 0);
static_assert(compute_member_index<MramLayout, mram_log_t>() == 3);
static_assert(compute_member_index<MramLayout, mram_unused_t>() == static_cast<std::size_t>(-1));

static_assert(MramRegistry<MramLayout>::lookup("state") != nullptr);
static_assert(MramRegistry<MramLayout>::lookup("state")->index == 2);
static_assert(MramRegistry<MramLayout>::lookup_by_index(1) != nullptr);
static_assert(MramRegistry<MramLayout>::lookup_by_index(1)->offset == offsetof(MramLayout, command));
static_assert(MramRegistry<MramLayout>::lookup_by_index(99) == nullptr);
static_assert(MramRegistry<MramLayout>::entry_for<mram_command_t>().index == 1);

// ------------------------------------------------------------
// Tests for traits
// ------------------------------------------------------------
TEST_CASE("MramTraits resolves correct offsets")
{
    using L = MramLayout;

    CHECK(MramTraits<mram_config_t, L>::offset == offsetof(L, config));
    CHECK(MramTraits<mram_command_t, L>::offset == offsetof(L, command));
    CHECK(MramTraits<mram_state_t, L>::offset == offsetof(L, state));
    CHECK(MramTraits<mram_log_t, L>::offset == offsetof(L, log));
}

TEST_CASE("MramTraits resolves correct versions")
{
    using L = MramLayout;

    CHECK(MramTraits<mram_config_t, L>::version == 1);
    CHECK(MramTraits<mram_command_t, L>::version == 1);
    CHECK(MramTraits<mram_state_t, L>::version == 1);
    CHECK(MramTraits<mram_log_t, L>::version == 1);
}

TEST_CASE("MramTraits resolves correct member index")
{
    using L = MramLayout;

    CHECK(MramTraits<mram_config_t, L>::index == 0);
    CHECK(MramTraits<mram_command_t, L>::index == 1);
    CHECK(MramTraits<mram_state_t, L>::index == 2);
    CHECK(MramTraits<mram_log_t, L>::index == 3);
}

// ------------------------------------------------------------
// Tests for MramRegistry
// ------------------------------------------------------------
TEST_CASE("MramRegistry builds correct number of entries")
{
    constexpr auto& entries = MramRegistry<MramLayout>::entries;
    CHECK(entries.size() == 4);
}

TEST_CASE("MramRegistry entries match MramTraits")
{
    constexpr auto& entries = MramRegistry<MramLayout>::entries;

    CHECK(entries[0].name           == std::string_view("config"));
    CHECK(entries[0].offset         == MramTraits<mram_config_t, MramLayout>::offset);
    CHECK(entries[0].size           == sizeof(MramRecord<mram_config_t>));
    CHECK(entries[0].payload_offset == offsetof(MramRecord<mram_config_t>, payload));
    CHECK(entries[0].payload_size   == sizeof(mram_config_t));
    CHECK(entries[0].index          == 0);
    CHECK(entries[0].version        == 1);

    CHECK(entries[1].name           == std::string_view("command"));
    CHECK(entries[1].offset         == MramTraits<mram_command_t, MramLayout>::offset);
    CHECK(entries[1].size           == sizeof(MramRecord<mram_command_t>));
    CHECK(entries[1].payload_offset == offsetof(MramRecord<mram_command_t>, payload));
    CHECK(entries[1].payload_size   == sizeof(mram_command_t));
    CHECK(entries[1].index          == 1);
    CHECK(entries[1].version        == 1);

    CHECK(entries[2].name           == std::string_view("state"));
    CHECK(entries[2].offset         == MramTraits<mram_state_t, MramLayout>::offset);
    CHECK(entries[2].size           == sizeof(MramRecord<mram_state_t>));
    CHECK(entries[2].payload_offset == offsetof(MramRecord<mram_state_t>, payload));
    CHECK(entries[2].payload_size   == sizeof(mram_state_t));
    CHECK(entries[2].index          == 2);
    CHECK(entries[2].version        == 1);

    CHECK(entries[3].name           == std::string_view("log"));
    CHECK(entries[3].offset         == MramTraits<mram_log_t, MramLayout>::offset);
    CHECK(entries[3].size           == sizeof(MramRecord<mram_log_t>));
    CHECK(entries[3].payload_offset == offsetof(MramRecord<mram_log_t>, payload));
    CHECK(entries[3].payload_size   == sizeof(mram_log_t));
    CHECK(entries[3].index          == 3);
    CHECK(entries[3].version        == 1);
}

TEST_CASE("MramRegistry lookup by name works")
{
    using Reg = MramRegistry<MramLayout>;

    auto* e0 = Reg::lookup("config");
    REQUIRE(e0 != nullptr);
    CHECK(e0->index == 0);

    auto* e1 = Reg::lookup("command");
    REQUIRE(e1 != nullptr);
    CHECK(e1->index == 1);

    auto* e2 = Reg::lookup("state");
    REQUIRE(e2 != nullptr);
    CHECK(e2->index == 2);

    auto* e3 = Reg::lookup("log");
    REQUIRE(e3 != nullptr);
    CHECK(e3->index == 3);

    CHECK(Reg::lookup("does_not_exist") == nullptr);
    CHECK(Reg::lookup("") == nullptr);
}

TEST_CASE("MramRegistry lookup_by_index works")
{
    using Reg = MramRegistry<MramLayout>;

    auto* e0 = Reg::lookup_by_index(0);
    REQUIRE(e0 != nullptr);
    CHECK(e0->name == std::string_view("config"));

    auto* e1 = Reg::lookup_by_index(1);
    REQUIRE(e1 != nullptr);
    CHECK(e1->name == std::string_view("command"));

    auto* e2 = Reg::lookup_by_index(2);
    REQUIRE(e2 != nullptr);
    CHECK(e2->name == std::string_view("state"));

    auto* e3 = Reg::lookup_by_index(3);
    REQUIRE(e3 != nullptr);
    CHECK(e3->name == std::string_view("log"));

    CHECK(Reg::lookup_by_index(4) == nullptr);
}

TEST_CASE("MramRegistry entry_for<T> returns matching entry")
{
    using Reg = MramRegistry<MramLayout>;

    CHECK(&Reg::entry_for<mram_config_t>()  == &Reg::entries[0]);
    CHECK(&Reg::entry_for<mram_command_t>() == &Reg::entries[1]);
    CHECK(&Reg::entry_for<mram_state_t>()   == &Reg::entries[2]);
    CHECK(&Reg::entry_for<mram_log_t>()     == &Reg::entries[3]);
}

// ------------------------------------------------------------
// Second layout: distinct versions and stricter alignment
// ------------------------------------------------------------
struct alignas(32) aligned_payload_t
{
    std::uint64_t x;
    std::uint8_t  y;
};

struct small_payload_t
{
    std::uint8_t byte;
};

struct AlignedMramLayout
{
    MramRecord<small_payload_t>   first;
    MramRecord<aligned_payload_t> second;
};

template <>
struct MramLayoutMembers<AlignedMramLayout>
{
    static constexpr auto members = std::make_tuple(
        &AlignedMramLayout::first,
        &AlignedMramLayout::second
    );
};

template <>
struct MramTraits<small_payload_t, AlignedMramLayout>
{
    static constexpr uint32_t offset  = offsetof(AlignedMramLayout, first);
    static constexpr uint32_t version = 2;
    static constexpr std::size_t index = 0;
    static constexpr const char* name = "first";
};

template <>
struct MramTraits<aligned_payload_t, AlignedMramLayout>
{
    static constexpr uint32_t offset  = offsetof(AlignedMramLayout, second);
    static constexpr uint32_t version = 7;
    static constexpr std::size_t index = 1;
    static constexpr const char* name = "second";
};

TEST_CASE("MramRegistry handles distinct versions and aligned payloads")
{
    using Reg = MramRegistry<AlignedMramLayout>;

    const auto& first  = Reg::entry_for<small_payload_t>();
    const auto& second = Reg::entry_for<aligned_payload_t>();

    CHECK(first.version        == 2);
    CHECK(first.payload_size   == sizeof(small_payload_t));
    CHECK(first.payload_offset == offsetof(MramRecord<small_payload_t>, payload));

    CHECK(second.version        == 7);
    CHECK(second.offset         == offsetof(AlignedMramLayout, second));
    CHECK(second.payload_size   == sizeof(aligned_payload_t));
    CHECK(second.payload_offset == offsetof(MramRecord<aligned_payload_t>, payload));
    CHECK(second.payload_offset >= 16); // header is 4 x uint32_t, padded to 32-byte alignment
}