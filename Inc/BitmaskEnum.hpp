#pragma once
#include <concepts>
#include <cstdint>
#include <type_traits>

// 1. Define a C++20 Concept that checks if a type is an enum AND has opted in
template<typename T>
concept BitmaskEnum = std::is_enum_v<T> && requires(T e) {
    { enable_bitmask_operators(e) } -> std::same_as<std::true_type>;
};

// 2. Macro that works in ANY namespace (global, custom namespace, or even inside a class/struct!)
#define ENABLE_BITMASK_OPERATORS(EnumType) \
    consteval std::true_type enable_bitmask_operators(EnumType) noexcept { return {}; }

// 3. Clean C++20 constrained bitwise operators
template<BitmaskEnum Enum>
constexpr Enum operator|(Enum lhs, Enum rhs) noexcept {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

template<BitmaskEnum Enum>
constexpr Enum operator&(Enum lhs, Enum rhs) noexcept {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) & static_cast<T>(rhs));
}

template<BitmaskEnum Enum>
constexpr Enum operator^(Enum lhs, Enum rhs) noexcept {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) ^ static_cast<T>(rhs));
}

template<BitmaskEnum Enum>
constexpr Enum operator~(Enum rhs) noexcept {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<T>(rhs));
}

template<BitmaskEnum Enum>
constexpr Enum& operator|=(Enum& lhs, Enum rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

template<BitmaskEnum Enum>
constexpr Enum& operator&=(Enum& lhs, Enum rhs) noexcept {
    lhs = lhs & rhs;
    return lhs;
}

// 4. Helper for checking flags cleanly
template<BitmaskEnum Enum>
constexpr bool has_flag(Enum value, Enum flag) noexcept {
    using T = std::underlying_type_t<Enum>;
    return (static_cast<T>(value) & static_cast<T>(flag)) != 0;
}
