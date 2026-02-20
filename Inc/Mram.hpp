// Mram.hpp - MRAM driver header

#pragma once
#include <tuple>
#include <cstddef>
#include <type_traits>
#include <optional>

// ------------------------------------------------------------
// 1. Generic metadata wrapper
// ------------------------------------------------------------
template <typename T>
struct MramRecord
{
    uint32_t version{};
    uint32_t crc{};
    uint32_t seq{};
    uint32_t flags{};
    T        payload{};
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
// ------------------------------------------------------------
template <typename Layout>
struct MramLayoutMembers;   // user will specialize this

// ------------------------------------------------------------
// 3. Compile-time search for member index by payload type T
// ------------------------------------------------------------
template <typename T, typename Layout, std::size_t... I>
constexpr std::optional<std::size_t>
find_member_index_impl(std::index_sequence<I...>)
{
    std::optional<std::size_t> result;

    // Fold expression: check each member
    ((std::is_same_v<typename payload_of<
                          std::remove_reference_t<
                              decltype(std::declval<Layout>().*
                                       std::get<I>(MramLayoutMembers<Layout>::members))
                          >
                      >::type,
                      T>
          ? result = I
          : void()),
     ...);

    return result;
}

template <typename T, typename Layout>
constexpr std::size_t find_member_index()
{
    constexpr std::size_t N =
        std::tuple_size_v<decltype(MramLayoutMembers<Layout>::members)>;

    auto idx = find_member_index_impl<T, Layout>(std::make_index_sequence<N>{});
    static_assert(idx.has_value(), "Type T not found in MramLayout");
    return *idx;
}

// ------------------------------------------------------------
// 4. Generic MramTraits<T> derived from MramLayout
// ------------------------------------------------------------
template <typename T, typename Layout>
struct MramTraitsBase
{
    static constexpr std::size_t index = find_member_index<T, Layout>();

    static constexpr auto member_ptr =
        std::get<index>(MramLayoutMembers<Layout>::members);

    static constexpr uint32_t offset =
        static_cast<uint32_t>(offsetof(Layout, *member_ptr));

    static constexpr uint32_t version = 1;
};

// User-facing alias: MramTraits<T> requires Layout to be defined
template <typename T, typename Layout>
using MramTraits = MramTraitsBase<T, Layout>;

