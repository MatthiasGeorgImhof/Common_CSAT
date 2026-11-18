// Register.hpp
#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <tuple>
#include "RecordRegistry.hpp"

// User specializes this for each payload type T (or provides a generic unstructured fallback)
template <typename T, typename RegisterValue>
struct RegisterCodec;
// Expected interface:
//   static bool encode(const T& payload, RegisterValue& out);
//   static bool decode(const RegisterValue& in, T& payload);

template <typename T, typename RegisterValue>
concept RegisterCodecConcept =
    requires(const T& ct, T& t, const RegisterValue& crv, RegisterValue& rv)
    {
        { RegisterCodec<T, RegisterValue>::encode(ct, rv) } -> std::same_as<bool>;
        { RegisterCodec<T, RegisterValue>::decode(crv, t) } -> std::same_as<bool>;
    };

template <typename RegisterValue>
struct RegisterEntry
{
    const RecordRegistryEntry* entry;
    bool (*encode_raw)(const void* payload, RegisterValue& out);
    bool (*decode_raw)(const RegisterValue& in, void* payload);
};

template <typename Layout, typename RegisterValue>
struct RegisterTable
{
    static constexpr auto build()
    {
        constexpr auto tup = LayoutMembers<Layout>::members;

        return []<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<RegisterEntry<RegisterValue>, sizeof...(I)>{
                []<std::size_t Idx>() {
                    using T = typename member_payload<Layout, decltype(std::get<Idx>(tup))>::type;

                    static_assert(RegisterCodecConcept<T, RegisterValue>,
                                  "Missing or invalid RegisterCodec<T, RegisterValue> specialization");

                    return RegisterEntry<RegisterValue>{
                        &RecordRegistry<Layout>::template entry_for<T>(),
                        [](const void* p, RegisterValue& out) -> bool {
                            if (!p)
                                return false;
                            return RegisterCodec<T, RegisterValue>::encode(
                                *static_cast<const T*>(p), out);
                        },
                        [](const RegisterValue& in, void* p) -> bool {
                            if (!p)
                                return false;
                            return RegisterCodec<T, RegisterValue>::decode(
                                in, *static_cast<T*>(p));
                        }
                    };
                }.template operator()<I>()...
            };
        }(std::make_index_sequence<std::tuple_size_v<decltype(tup)>>{});
    }

    static constexpr auto registers = build();

    static constexpr const RegisterEntry<RegisterValue>* lookup(std::string_view name)
    {
        for (auto const& r : registers)
            if (name == r.entry->name)
                return &r;
        return nullptr;
    }

    static constexpr const RegisterEntry<RegisterValue>* lookup_by_index(std::size_t index)
    {
        for (auto const& r : registers)
            if (r.entry->index == index)
                return &r;
        return nullptr;
    }
};