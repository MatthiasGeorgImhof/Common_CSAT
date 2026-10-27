#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Serialization.hpp"
#include "canard.h"

TEST_CASE("HeaderFrame layout")
{
    CHECK(sizeof(HeaderFrame<8>) == 14);
    CHECK(sizeof(COBSHeaderFrame<8>) == 17);
}

TEST_CASE("PlainFramer serialize")
{
    PlainFramer<8> framer;

    uint8_t payload[8] = {1,2,3,4,5,6,7,8};

    CanardMutableFrame frame {};
    frame.extended_can_id = 0x12345678;
    frame.payload.size = 8;
    frame.payload.data = payload;

    auto header = framer.serialize(frame);

    uint32_t id = header.extended_can_id;

    CHECK(id == 0x12345678);
    CHECK(std::memcmp(header.payload, payload, 8) == 0);
}

TEST_CASE("PlainFramer serialized_size")
{
    uint8_t payload[3] = {1,2,3};

    HeaderFrame<8> frame {};
    frame.extended_can_id = 0x12345678;
    frame.size = sizeof(payload);
    memcpy(frame.payload, payload, sizeof(payload));

    CHECK(serialized_size<8>(frame) == sizeof(payload)+ 6);
}

TEST_CASE("PlainFramer deserialize")
{
    const uint16_t SIZE =7;
    PlainFramer<8> framer;

    HeaderFrame<8> header {};
    header.extended_can_id = 0x12345678;

    for(size_t i = 0; i < SIZE; ++i)
    {
        header.payload[i] = static_cast<uint8_t>(i);
    }
    header.size = SIZE;

    CanardFrame frame = framer.deserialize(header);

    CHECK(frame.extended_can_id == 0x12345678);
    CHECK(frame.payload.size == 7);
    CHECK(std::memcmp(frame.payload.data, header.payload, SIZE) == 0);
}

TEST_CASE("PlainFramer round trip")
{
    PlainFramer<8> framer;

    uint8_t payload[8] = {1,2,3,4,5,6,7,8};

    CanardMutableFrame in {};
    in.extended_can_id = 0x12345678;
    in.payload.size = 8;
    in.payload.data = payload;

    HeaderFrame<8> header = framer.serialize(in);
    CanardFrame out = framer.deserialize(header);

    CHECK(out.extended_can_id == in.extended_can_id);
    CHECK(out.payload.size == 8);
    CHECK(std::memcmp(out.payload.data, payload, 8) == 0);
}

TEST_CASE("COBSFramer serialize")
{
    COBSFramer<8> framer;

    uint8_t payload[8] = {1,2,3,4,5,6,7,8};

    CanardMutableFrame frame {};
    frame.extended_can_id = 0x12345678;
    frame.payload.size = 8;
    frame.payload.data = payload;

    auto cobs_frame = framer.serialize(frame);

    CHECK(cobs_frame.start_of_frame == 0x00);
    CHECK(cobs_frame.end_of_frame == 0x00);
    CHECK(cobs_frame.cobs != 0);

    CanardFrame decoded = framer.deserialize(cobs_frame);

    CHECK(decoded.extended_can_id == frame.extended_can_id);
    CHECK(decoded.payload.size == 8);
    CHECK(std::memcmp(decoded.payload.data, payload, 8) == 0);
}

TEST_CASE("COBSFramer serialize deserialize round trip")
{
    COBSFramer<8> framer;

    uint8_t payload[8] = {1,2,3,4,5,6,7,8};

    CanardMutableFrame input {};
    input.extended_can_id = 0x12345678;
    input.payload.size = 8;
    input.payload.data = payload;

    auto encoded = framer.serialize(input);
    auto output = framer.deserialize(encoded);

    CHECK(output.extended_can_id == input.extended_can_id);
    CHECK(output.payload.size == input.payload.size);
    CHECK(std::memcmp(output.payload.data, payload, 8) == 0);
}

TEST_CASE("COBSFramer serialize preserves framing bytes")
{
    COBSFramer<8> framer;

    uint8_t payload[8] = {1,2,3,4,5,6,7,8};

    CanardMutableFrame frame {};
    frame.extended_can_id = 0x12345678;
    frame.payload.size = 8;
    frame.payload.data = payload;

    auto cobs_frame = framer.serialize(frame);

    CHECK(cobs_frame.start_of_frame == 0x00);
    CHECK(cobs_frame.end_of_frame == 0x00);
    CHECK(cobs_frame.cobs != 0);
}

TEST_CASE("COBSFramer round trip without zeros")
{
    COBSFramer<8> framer;

    uint8_t payload[8] = {1,2,3,4,5,6,7,8};

    CanardMutableFrame in {};
    in.extended_can_id = 0x12345678;
    in.payload.size = 8;
    in.payload.data = payload;

    auto wire = framer.serialize(in);
    auto out = framer.deserialize(wire);

    CHECK(out.extended_can_id == 0x12345678);
    CHECK(out.payload.size == 8);
    CHECK(std::memcmp(out.payload.data, payload, 8) == 0);
}

TEST_CASE("COBSFramer round trip with zeros")
{
    COBSFramer<8> framer;

    uint8_t payload[8] = {0,1,0,2,0,3,0,4};

    CanardMutableFrame in {};
    in.extended_can_id = 0x12003400;
    in.payload.size = 8;
    in.payload.data = payload;

    auto wire = framer.serialize(in);
    auto out = framer.deserialize(wire);

    CHECK(out.extended_can_id == 0x12003400);
    CHECK(out.payload.size == 8);
    CHECK(std::memcmp(out.payload.data, payload, 8) == 0);
}

TEST_CASE("COBSFramer all zero payload")
{
    COBSFramer<8> framer;

    uint8_t payload[8] = {0,0,0,0,0,0,0,0};

    CanardMutableFrame in {};
    in.extended_can_id = 0;
    in.payload.size = 8;
    in.payload.data = payload;

    auto wire = framer.serialize(in);
    auto out = framer.deserialize(wire);

    CHECK(out.extended_can_id == 0);
    CHECK(out.payload.size == 8);
    CHECK(std::memcmp(out.payload.data, payload, 8) == 0);
}

TEST_CASE("COBSFramer all zero header frame")
{
    COBSFramer<8> framer;

    uint8_t payload[8] = {0,0,0,0,0,0,0,0};

    CanardMutableFrame in {};
    in.extended_can_id = 0;
    in.payload.size = 8;
    in.payload.data = payload;

    auto wire = framer.serialize(in);

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&wire.header_frame);

    for(size_t i = 0; i < sizeof(wire.header_frame); ++i)
    {
        CAPTURE(i);
        CHECK(bytes[i] != 0);
    }
}

TEST_CASE("COBSFramer exhaustive zero positions")
{
    COBSFramer<8> framer;

    for(size_t zero_pos = 0; zero_pos < 8; ++zero_pos)
    {
        uint8_t payload[8] = {1,2,3,4,5,6,7,8};

        payload[zero_pos] = 0;

        CanardMutableFrame in {};
        in.extended_can_id = 0x12345678;
        in.payload.size = 8;
        in.payload.data = payload;

        auto wire = framer.serialize(in);
        auto out = framer.deserialize(wire);

        CAPTURE(zero_pos);

        CHECK(out.extended_can_id == 0x12345678);
        CHECK(std::memcmp(out.payload.data, payload, 8) == 0);
    }
}

