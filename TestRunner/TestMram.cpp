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
// Tests
// ------------------------------------------------------------
TEST_CASE("MramTraits resolves correct offsets")
{
    using L = MramLayout;

    CHECK(MramTraits<mram_config_t, L>::offset ==
          offsetof(L, config));

    CHECK(MramTraits<mram_command_t, L>::offset ==
          offsetof(L, command));

    CHECK(MramTraits<mram_state_t, L>::offset ==
          offsetof(L, state));

    CHECK(MramTraits<mram_log_t, L>::offset ==
          offsetof(L, log));
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
