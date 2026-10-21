#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

template<size_t MTU>
struct DataFrame
{
    static constexpr size_t Capacity = MTU;

    size_t size = 0;
    uint8_t data[MTU] = {};

    constexpr uint8_t* payload()
    {
        return data;
    }

    constexpr const uint8_t* payload() const
    {
        return data;
    }

    constexpr void clear()
    {
        size = 0;
    }

    bool assign(const uint8_t* src, size_t len)
    {
        if (len > MTU)
        {
            return false;
        }

        std::memcpy(data, src, len);
        size = len;

        return true;
    }
};