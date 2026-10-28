#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "cyphal_mtu.hpp"
#include "CircularBuffer.hpp"
#include "Serialization.hpp"
#include "Socket.hpp"
#include "RxQueueFiller.hpp"

TEST_CASE("UdpRxQueueFiller")
{
    using Frame = HeaderFrame<LARGE_DATA_MTU>;
    using Buffer = CircularBuffer<Frame, 8>;

    struct MockSocket
    {
        bool consumed = false;

        bool poll()
        {
            return !consumed;
        }

        int32_t receive(uint8_t* data, uint16_t size, Endpoint&)
        {
            REQUIRE(size == sizeof(Frame));

            Frame frame{};
            frame.extended_can_id = 0x12345678;
            frame.size = 4;

            frame.payload[0] = 1;
            frame.payload[1] = 2;
            frame.payload[2] = 3;
            frame.payload[3] = 4;

            memcpy(data, &frame, sizeof(frame));

            consumed = true;

            return sizeof(frame);
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    CHECK(buffer.size() == 0);

    filler.fill();

    CHECK(buffer.size() == 1);

    auto frame = buffer.pop();

    const uint32_t extended_can_id = frame.extended_can_id;
    const uint16_t size = frame.size;
    CHECK(extended_can_id == 0x12345678);
    CHECK(size == 4);
    
    CHECK(frame.payload[0] == 1);
    CHECK(frame.payload[1] == 2);
    CHECK(frame.payload[2] == 3);
    CHECK(frame.payload[3] == 4);
}

TEST_CASE("UdpRxQueueFiller multiple packets")
{
    using Frame = HeaderFrame<LARGE_DATA_MTU>;
    using Buffer = CircularBuffer<Frame, 8>;

    struct MockSocket
    {
        unsigned int count = 0;

        bool poll()
        {
            return count < 3;
        }

        int32_t receive(uint8_t* data, uint16_t, Endpoint&)
        {
            Frame frame{};
            frame.extended_can_id = count;
            frame.size = 1;
            frame.payload[0] = static_cast<uint8_t>(count);

            memcpy(data, &frame, sizeof(frame));

            count++;
            return sizeof(frame);
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(buffer.size() == 3);

    CHECK(buffer.pop().payload[0] == 0);
    CHECK(buffer.pop().payload[0] == 1);
    CHECK(buffer.pop().payload[0] == 2);
}