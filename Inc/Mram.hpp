// Mram.hpp - MRAM driver header

#pragma once
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <string_view>
#include <array>

// ------------------------------------------------------------
// 1. Generic MRAM record wrapper
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

// Extract payload type from MramRecord<T>
template <typename Record>
struct payload_of;

template <typename T>
struct payload_of<MramRecord<T>>
{
    using type = T;
};

// ------------------------------------------------------------
// 2. Mission-specific declarations (forward only)
// ------------------------------------------------------------

template <typename Layout>
struct MramLayoutMembers;   // user specializes

template <typename T, typename Layout>
struct MramTraits;          // user specializes

// ------------------------------------------------------------
// 3. Traits concept (compile-time validation of specializations)
// ------------------------------------------------------------

template <typename Traits>
concept MramTraitsConcept =
    requires {
        { Traits::offset }  -> std::convertible_to<std::uint32_t>;
        { Traits::version } -> std::convertible_to<std::uint32_t>;
        { Traits::index }   -> std::convertible_to<std::size_t>;
        { Traits::name }    -> std::same_as<const char*>;
    };

// ------------------------------------------------------------
// 4. Member-pointer payload extraction
// ------------------------------------------------------------

template <typename Layout, typename MemberPtr>
struct member_payload;

template <typename Layout, typename T>
struct member_payload<Layout, MramRecord<T> Layout::*>
{
    using type = T;
};

template <typename Layout, typename T>
struct member_payload<Layout, MramRecord<T> Layout::* const>
{
    using type = T;
};

template <typename Layout, typename T>
struct member_payload<Layout, MramRecord<T> Layout::* const&>
{
    using type = T;
};

// ------------------------------------------------------------
// 5. Compute tuple index of a payload type (optional helper)
// ------------------------------------------------------------

template <typename Layout, typename T>
constexpr std::size_t compute_member_index()
{
    constexpr auto tup = MramLayoutMembers<Layout>::members;

    return []<std::size_t... I>(std::index_sequence<I...>) {
        std::size_t result = static_cast<std::size_t>(-1);
        ((std::is_same_v<T,
            typename member_payload<Layout, decltype(std::get<I>(tup))>::type>
            ? result = I : 0), ...);
        return result;
    }(std::make_index_sequence<std::tuple_size_v<decltype(tup)>>{});
}

// ------------------------------------------------------------
// 6. Registry entry type
// ------------------------------------------------------------

struct MramRegistryEntry
{
    const char*   name;
    std::uint32_t offset;
    std::size_t   size;
    std::size_t   index;
};

// ------------------------------------------------------------
// 7. Generic MRAM registry builder
// ------------------------------------------------------------

template <typename Layout>
struct MramRegistry
{
    // Build entries from MramLayoutMembers<Layout> and MramTraits<T, Layout>
    static constexpr auto build_entries()
    {
        constexpr auto tup = MramLayoutMembers<Layout>::members;

        return []<std::size_t... I>(std::index_sequence<I...>) {
            return std::array{
                MramRegistryEntry{
                    MramTraits<
                        typename member_payload<Layout, decltype(std::get<I>(tup))>::type,
                        Layout
                    >::name,
                    MramTraits<
                        typename member_payload<Layout, decltype(std::get<I>(tup))>::type,
                        Layout
                    >::offset,
                    sizeof(MramRecord<
                        typename member_payload<Layout, decltype(std::get<I>(tup))>::type
                    >),
                    MramTraits<
                        typename member_payload<Layout, decltype(std::get<I>(tup))>::type,
                        Layout
                    >::index
                }...
            };
        }(std::make_index_sequence<std::tuple_size_v<decltype(tup)>>{});
    }

    static constexpr auto entries = build_entries();

    // --------------------------------------------------------
    // 8. Minimal compile-time validation: every member has traits
    // --------------------------------------------------------

    // template <std::size_t... I>
    // static consteval bool all_traits_present(std::index_sequence<I...>)
    // {
    //     constexpr auto tup = MramLayoutMembers<Layout>::members;
    //     return (MramTraitsConcept<
    //                 MramTraits<
    //                     typename member_payload<Layout, decltype(std::get<I>(tup))>::type,
    //                     Layout
    //                 >
    //             > && ...);
    // }

    // static_assert(
    //     all_traits_present(
    //         std::make_index_sequence<
    //             std::tuple_size_v<decltype(MramLayoutMembers<Layout>::members)>
    //         >{}
    //     ),
    //     "Missing MramTraits specialization for some MRAM record"
    // );

    // --------------------------------------------------------
    // 9. Lookup by name
    // --------------------------------------------------------

    static const MramRegistryEntry* lookup(std::string_view name)
    {
        for (auto const& e : entries)
            if (name == e.name)
                return &e;
        return nullptr;
    }
};
