#pragma once

#include "canard.h"
#include "cobs.hpp"

template <size_t N>
struct __attribute__((packed)) HeaderFrame
{
    uint32_t extended_can_id;
    uint16_t size;
    uint8_t payload[N];
};
static_assert(sizeof(HeaderFrame<8>) == 14, "HeaderFrame<8> should be 14 bytes");

template <size_t N>
struct __attribute__((packed)) COBSHeaderFrame
{
    static_assert(
        sizeof(HeaderFrame<N>) <= cobs::max_in_place_size, "HeaderFrame exceeds COBS block limit");

    uint8_t start_of_frame = 0x00;
    uint8_t cobs;
    HeaderFrame<N> header_frame;
    uint8_t end_of_frame = 0x00;
};
static_assert(sizeof(COBSHeaderFrame<8>) == 17, "COBSHeaderFrame<8> should be 17 bytes");


template <size_t N>
struct PlainFramer
{ 
    HeaderFrame<N> serialize(const CanardMutableFrame& frame)
    {
        HeaderFrame<N> header_frame;
        header_frame.extended_can_id = frame.extended_can_id;
        memcpy(header_frame.payload, frame.payload.data, frame.payload.size);

        return header_frame;
    }


    CanardFrame deserialize(const HeaderFrame<N>& header_frame)
    {
        CanardFrame frame;
        frame.extended_can_id = header_frame.extended_can_id;
        frame.payload.size = N;
        frame.payload.data = header_frame.payload;
        return frame;
    }
};

template <size_t N>
struct COBSFramer
{ 
    static_assert(sizeof(HeaderFrame<N>) <= cobs::max_in_place_size, "HeaderFrame is too large for this in-place COBS encoding");

    COBSHeaderFrame<N> serialize(const CanardMutableFrame& frame)
    {
        COBSHeaderFrame<N> cobs_frame;
        cobs_frame.header_frame.extended_can_id = frame.extended_can_id;
        memcpy(cobs_frame.header_frame.payload, frame.payload.data, frame.payload.size);

        cobs_frame.cobs = cobs::encode_in_place(reinterpret_cast<uint8_t*>(&cobs_frame.header_frame), sizeof(cobs_frame.header_frame));

        return cobs_frame;
    }


    CanardFrame deserialize(COBSHeaderFrame<N>& cobs_frame)
    {
        cobs::decode_in_place(cobs_frame.cobs, reinterpret_cast<uint8_t*>(&cobs_frame.header_frame), sizeof(cobs_frame.header_frame));

        CanardFrame frame;
        frame.extended_can_id = cobs_frame.header_frame.extended_can_id;
        frame.payload.size = N;
        frame.payload.data = cobs_frame.header_frame.payload;
        return frame;
    }
};

template<size_t N>
HeaderFrame<N>& GetHeaderFrame(HeaderFrame<N>& frame)
{
    return frame;
}

template<size_t N>
HeaderFrame<N>& GetHeaderFrame(COBSHeaderFrame<N>& frame)
{
    cobs::decode_in_place(frame.cobs, reinterpret_cast<uint8_t*>(&frame.header_frame));
    return frame.header_frame;
}
