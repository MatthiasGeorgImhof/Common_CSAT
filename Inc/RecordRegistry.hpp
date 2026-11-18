// RecordRegistry.hpp - Record driver header

#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

// ------------------------------------------------------------
// 1. Generic Record record wrapper
// ------------------------------------------------------------

template <typename T>
struct StorageRecord
{
    std::uint32_t version{};
    std::uint32_t crc{};
    std::uint32_t seq{};
    std::uint32_t flags{};
    T             payload{};
};

// Extract payload type from StorageRecord<T>
template <typename Record>
struct payload_of;

template <typename T>
struct payload_of<StorageRecord<T>>
{
    using type = T;
};

// ------------------------------------------------------------
// 2. Mission-specific declarations (forward only)
// ------------------------------------------------------------

template <typename Layout>
struct LayoutMembers;   // user specializes

template <typename T, typename Layout>
struct RecordTraits;          // user specializes

// ------------------------------------------------------------
// 3. Traits concept (compile-time validation of specializations)
// ------------------------------------------------------------

template <typename Traits>
concept RecordTraitsConcept =
    requires {
        { Traits::offset }  -> std::convertible_to<std::uint32_t>;
        { Traits::version } -> std::convertible_to<std::uint32_t>;
        { Traits::index }   -> std::convertible_to<std::size_t>;
        { Traits::name }    -> std::convertible_to<const char*>;
    };

// ------------------------------------------------------------
// 4. Member-pointer payload extraction
// ------------------------------------------------------------

template <typename Layout, typename MemberPtr>
struct member_payload;

template <typename Layout, typename T>
struct member_payload<Layout, StorageRecord<T> Layout::*>
{
    using type = T;
};

template <typename Layout, typename T>
struct member_payload<Layout, StorageRecord<T> Layout::* const>
{
    using type = T;
};

template <typename Layout, typename T>
struct member_payload<Layout, StorageRecord<T> Layout::* const&>
{
    using type = T;
};

// ------------------------------------------------------------
// 5. Compute tuple index of a payload type
// ------------------------------------------------------------

template <typename Layout, typename T>
constexpr std::size_t compute_member_index()
{
    constexpr auto tup = LayoutMembers<Layout>::members;

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

struct RecordRegistryEntry
{
    const char*   name;
    std::uint32_t offset;         // offset of StorageRecord<T> in Layout
    std::size_t   size;           // sizeof(StorageRecord<T>)
    std::size_t   payload_offset; // offsetof(StorageRecord<T>, payload)
    std::size_t   payload_size;   // sizeof(T)
    std::size_t   index;
    std::uint32_t version;
};

// ------------------------------------------------------------
// 7. Generic Record registry builder
// ------------------------------------------------------------

template <typename Layout>
struct RecordRegistry
{
    static constexpr auto build_entries()
    {
        constexpr auto tup = LayoutMembers<Layout>::members;

        return []<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<RecordRegistryEntry, sizeof...(I)>{
                []<std::size_t Idx>() {
                    using T = typename member_payload<Layout, decltype(std::get<Idx>(tup))>::type;
                    using Traits = RecordTraits<T, Layout>;

                    static_assert(std::is_standard_layout_v<T>,
                                  "Record payload T must be standard-layout");
                    static_assert(std::is_trivially_copyable_v<T>,
                                  "Record payload T must be trivially copyable");
                    static_assert(RecordTraitsConcept<Traits>,
                                  "Invalid or missing RecordTraits specialization");
                    static_assert(Traits::name != nullptr,
                                  "RecordTraits::name must not be null");

                    return RecordRegistryEntry{
                        Traits::name,
                        Traits::offset,
                        sizeof(StorageRecord<T>),
                        offsetof(StorageRecord<T>, payload),
                        sizeof(T),
                        Traits::index,
                        Traits::version
                    };
                }.template operator()<I>()...
            };
        }(std::make_index_sequence<std::tuple_size_v<decltype(tup)>>{});
    }

    static constexpr auto entries = build_entries();

    static consteval bool validate_entries()
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].name == nullptr || entries[i].name[0] == '\0')
                return false;

            for (std::size_t j = i + 1; j < entries.size(); ++j)
            {
                if (entries[i].index == entries[j].index)
                    return false;
                if (std::string_view(entries[i].name) == entries[j].name)
                    return false;
            }
        }

        // Ensure indices form a contiguous range 0 .. entries.size() - 1
        for (std::size_t expected = 0; expected < entries.size(); ++expected)
        {
            bool found = false;
            for (const auto& e : entries)
            {
                if (e.index == expected)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    }

    static_assert(validate_entries(),
                  "RecordRegistry entries must have non-empty unique names and unique indices");

    static constexpr const RecordRegistryEntry* lookup(std::string_view name)
    {
        for (auto const& e : entries)
            if (name == e.name)
                return &e;
        return nullptr;
    }

    static constexpr const RecordRegistryEntry* lookup_by_index(std::size_t index)
    {
        for (auto const& e : entries)
            if (e.index == index)
                return &e;
        return nullptr;
    }

    template <typename T>
    static constexpr const RecordRegistryEntry& entry_for()
    {
        constexpr auto idx = compute_member_index<Layout, T>();
        static_assert(idx != static_cast<std::size_t>(-1),
                      "Type T is not in LayoutMembers<Layout>");
        return entries[idx];
    }
};