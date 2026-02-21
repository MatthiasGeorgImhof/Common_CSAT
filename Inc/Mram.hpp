// Mram.hpp - MRAM driver header

#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <tuple>

// ------------------------------------------------------------
// 1. Generic metadata wrapper
// ------------------------------------------------------------

template <typename T>
struct MramRecord
{
    std::uint32_t version{};
    std::uint32_t crc{};
    std::uint32_t seq{};
    std::uint32_t flags{};
    T             payload{};
};

// Helper: extract payload type from MramRecord<T>
template <typename Record>
struct payload_of;

template <typename T>
struct payload_of<MramRecord<T>>
{
    using type = T;
};

// ------------------------------------------------------------
// 2. Mission-specific member list (forward declaration)
//    (used only if you want a tuple of member pointers)
// ------------------------------------------------------------

template <typename Layout>
struct MramLayoutMembers;   // user may specialize this if needed

// Forward declaration for registry used by TaskNram / tests
template <typename Layout>
struct MramRegistry;

// ------------------------------------------------------------
// 3. Mission-specific member traits list (forward declaration)
//    You provide explicit specializations in mission/test code.
// ------------------------------------------------------------

template <typename T, typename Layout>
struct MramTraits;
