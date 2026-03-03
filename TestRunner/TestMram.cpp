#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Mram.hpp"

// ------------------------------------------------------------
// Dummy payload types for testing
// ------------------------------------------------------------
struct mram_config_t  { int a; };
struct mram_command_t { int b; };
struct mram_state_t   { int c; };
struct mram_log_t     { int d; };

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

    CHECK(entries[0].name   == std::string_view("config"));
    CHECK(entries[0].offset == MramTraits<mram_config_t, MramLayout>::offset);
    CHECK(entries[0].size   == sizeof(MramRecord<mram_config_t>));
    CHECK(entries[0].index  == 0);

    CHECK(entries[1].name   == std::string_view("command"));
    CHECK(entries[1].offset == MramTraits<mram_command_t, MramLayout>::offset);
    CHECK(entries[1].size   == sizeof(MramRecord<mram_command_t>));
    CHECK(entries[1].index  == 1);

    CHECK(entries[2].name   == std::string_view("state"));
    CHECK(entries[2].offset == MramTraits<mram_state_t, MramLayout>::offset);
    CHECK(entries[2].size   == sizeof(MramRecord<mram_state_t>));
    CHECK(entries[2].index  == 2);

    CHECK(entries[3].name   == std::string_view("log"));
    CHECK(entries[3].offset == MramTraits<mram_log_t, MramLayout>::offset);
    CHECK(entries[3].size   == sizeof(MramRecord<mram_log_t>));
    CHECK(entries[3].index  == 3);
}

TEST_CASE("MramRegistry lookup works")
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

    auto* missing = Reg::lookup("does_not_exist");
    CHECK(missing == nullptr);
}

TEST_CASE("MramRegistry entry sizes match sizeof(MramRecord<T>)")
{
    constexpr auto& entries = MramRegistry<MramLayout>::entries;

    CHECK(entries[0].size == sizeof(MramRecord<mram_config_t>));
    CHECK(entries[1].size == sizeof(MramRecord<mram_command_t>));
    CHECK(entries[2].size == sizeof(MramRecord<mram_state_t>));
    CHECK(entries[3].size == sizeof(MramRecord<mram_log_t>));
}
