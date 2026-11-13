// MramRegister.hpp
#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <tuple>
#include "Mram.hpp"

// User specializes this for each payload type T (or provides a generic unstructured fallback)
template <typename T, typename RegisterValue>
struct MramRegisterCodec;
// Expected interface:
//   static bool encode(const T& payload, RegisterValue& out);
//   static bool decode(const RegisterValue& in, T& payload);

template <typename T, typename RegisterValue>
concept MramRegisterCodecConcept =
    requires(const T& ct, T& t, const RegisterValue& crv, RegisterValue& rv)
    {
        { MramRegisterCodec<T, RegisterValue>::encode(ct, rv) } -> std::same_as<bool>;
        { MramRegisterCodec<T, RegisterValue>::decode(crv, t) } -> std::same_as<bool>;
    };

template <typename RegisterValue>
struct MramRegisterEntry
{
    const MramRegistryEntry* mram;
    bool (*encode_raw)(const void* payload, RegisterValue& out);
    bool (*decode_raw)(const RegisterValue& in, void* payload);
};

template <typename Layout, typename RegisterValue>
struct MramRegisterTable
{
    static constexpr auto build()
    {
        constexpr auto tup = MramLayoutMembers<Layout>::members;

        return []<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<MramRegisterEntry<RegisterValue>, sizeof...(I)>{
                []<std::size_t Idx>() {
                    using T = typename member_payload<Layout, decltype(std::get<Idx>(tup))>::type;

                    static_assert(MramRegisterCodecConcept<T, RegisterValue>,
                                  "Missing or invalid MramRegisterCodec<T, RegisterValue> specialization");

                    return MramRegisterEntry<RegisterValue>{
                        &MramRegistry<Layout>::template entry_for<T>(),
                        [](const void* p, RegisterValue& out) -> bool {
                            if (!p)
                                return false;
                            return MramRegisterCodec<T, RegisterValue>::encode(
                                *static_cast<const T*>(p), out);
                        },
                        [](const RegisterValue& in, void* p) -> bool {
                            if (!p)
                                return false;
                            return MramRegisterCodec<T, RegisterValue>::decode(
                                in, *static_cast<T*>(p));
                        }
                    };
                }.template operator()<I>()...
            };
        }(std::make_index_sequence<std::tuple_size_v<decltype(tup)>>{});
    }

    static constexpr auto entries = build();

    static constexpr const MramRegisterEntry<RegisterValue>* lookup(std::string_view name)
    {
        for (auto const& e : entries)
            if (name == e.mram->name)
                return &e;
        return nullptr;
    }

    static constexpr const MramRegisterEntry<RegisterValue>* lookup_by_index(std::size_t index)
    {
        for (auto const& e : entries)
            if (e.mram->index == index)
                return &e;
        return nullptr;
    }
};