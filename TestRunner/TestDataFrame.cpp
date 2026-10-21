#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest/doctest.h"

#include "DataFrame.hpp"

using CanFrame      = DataFrame<8>;
using RadioFrame    = DataFrame<60>;
using EthernetFrame = DataFrame<254>;
using SerialFrame   = DataFrame<257>;

TEST_CASE("DataFrame default construction")
{
    DataFrame<254> frame;

    CHECK(frame.size == 0);
    CHECK(frame.Capacity == 254);

    for (size_t i = 0; i < frame.Capacity; ++i)
    {
        CHECK(frame.data[i] == 0);
    }
}

TEST_CASE("DataFrame payload returns data pointer")
{
    DataFrame<16> frame;

    CHECK(frame.payload() == frame.data);

    const DataFrame<16>& const_frame = frame;

    CHECK(const_frame.payload() == const_frame.data);
}

TEST_CASE("DataFrame clear resets size")
{
    DataFrame<16> frame;

    frame.size = 10;

    frame.clear();

    CHECK(frame.size == 0);
}

TEST_CASE("DataFrame assign copies payload")
{
    DataFrame<16> frame;

    uint8_t src[] = { 0x11, 0x22, 0x33, 0x44 };

    CHECK(frame.assign(src, sizeof(src)));

    CHECK(frame.size == sizeof(src));

    CHECK(frame.data[0] == 0x11);
    CHECK(frame.data[1] == 0x22);
    CHECK(frame.data[2] == 0x33);
    CHECK(frame.data[3] == 0x44);
}

TEST_CASE("DataFrame assign zero length")
{
    DataFrame<16> frame;

    CHECK(frame.assign(nullptr, 0));

    CHECK(frame.size == 0);
}

TEST_CASE("DataFrame assign fills entire frame")
{
    DataFrame<8> frame;

    uint8_t src[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

    CHECK(frame.assign(src, sizeof(src)));

    CHECK(frame.size == 8);

    for (size_t i = 0; i < 8; ++i)
    {
        CHECK(frame.data[i] == i);
    }
}

TEST_CASE("DataFrame assign rejects overflow")
{
    DataFrame<8> frame;

    uint8_t src[9] = {};

    CHECK_FALSE(frame.assign(src, sizeof(src)));

    CHECK(frame.size == 0);
}

TEST_CASE("CanFrame alias")
{
    CanFrame frame;

    static_assert(CanFrame::Capacity == 8);

    CHECK(frame.size == 0);
}

TEST_CASE("RadioFrame alias")
{
    RadioFrame frame;

    static_assert(RadioFrame::Capacity == 60);

    CHECK(frame.size == 0);
}

TEST_CASE("EthernetFrame alias")
{
    EthernetFrame frame;

    static_assert(EthernetFrame::Capacity == 254);

    CHECK(frame.size == 0);
}

TEST_CASE("SerialFrame alias")
{
    SerialFrame frame;

    static_assert(SerialFrame::Capacity == 257);

    CHECK(frame.size == 0);
}