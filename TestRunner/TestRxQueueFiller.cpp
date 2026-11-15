#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <deque>

#include "cyphal_mtu.hpp"
#include "CircularBuffer.hpp"
#include "Serialization.hpp"
#include "Socket.hpp"
#include "RxQueueFiller.hpp"

#include <deque>

using Frame  = HeaderFrame<LARGE_DATA_MTU>;
using Buffer = CircularBuffer<Frame, 8>;

struct Packet
{
    Frame frame;
    int32_t wire_len;
};

struct MockSocket
{
    std::deque<Packet> packets;

    bool opened = false;
    bool closed = false;

    uint16_t available() const
    {
        uint16_t total = 0;

        for (const auto& p : packets)
        {
            total += static_cast<uint16_t>(p.wire_len + 8);
        }

        return total;
    }

    bool open()
    {
        opened = true;
        return true;
    }

    void close()
    {
        closed = true;
    }

    bool reopen()
    {
        close();
        return open();
    }

    int32_t receive(uint8_t* data, uint16_t, Endpoint&)
    {
        REQUIRE_FALSE(packets.empty());

        auto packet = packets.front();
        packets.pop_front();

        std::memcpy(data, &packet.frame, sizeof(packet.frame));

        return packet.wire_len;
    }
};

TEST_CASE("UdpRxQueueFiller valid frame")
{
    using Buffer = CircularBuffer<Frame, 8>;

    MockSocket socket;
    Buffer buffer;

    Frame frame{};
    frame.extended_can_id = 0x12345678;
    frame.size = 4;

    frame.payload[0] = 1;
    frame.payload[1] = 2;
    frame.payload[2] = 3;
    frame.payload[3] = 4;

    socket.packets.push_back(
    {
        frame,
        static_cast<int32_t>(serialized_size(frame))
    });

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    REQUIRE(buffer.size() == 1);

    auto rx = buffer.pop();

    const uint32_t extended_can_id = rx.extended_can_id;
    const uint16_t size            = rx.size;

    CHECK(extended_can_id == 0x12345678);
    CHECK(size == 4);

    CHECK(rx.payload[0] == 1);
    CHECK(rx.payload[1] == 2);
    CHECK(rx.payload[2] == 3);
    CHECK(rx.payload[3] == 4);

    CHECK_FALSE(socket.closed);
}


TEST_CASE("UdpRxQueueFiller multiple valid frames")
{
    MockSocket socket;
    Buffer buffer;

    for (uint32_t i = 0; i < 3; i++)
    {
        Frame frame{};
        frame.extended_can_id = i;
        frame.size = 1;
        frame.payload[0] = static_cast<uint8_t>(i);

        socket.packets.push_back(
        {
            frame,
            static_cast<int32_t>(serialized_size(frame))
        });
    }

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    REQUIRE(buffer.size() == 3);

    CHECK(buffer.pop().payload[0] == 0);
    CHECK(buffer.pop().payload[0] == 1);
    CHECK(buffer.pop().payload[0] == 2);

    CHECK_FALSE(socket.closed);
}

TEST_CASE("UdpRxQueueFiller available underflow")
{
    struct MockSocket
    {
        bool opened = false;
        bool closed = false;

        uint16_t available() const
        {
            return 4;
        }

        bool open()
        {
            opened = true;
            return true;
        }

        void close()
        {
            closed = true;
        }

        bool reopen()
        {
            close();
            return open();
        }

        int32_t receive(uint8_t*, uint16_t, Endpoint&)
        {
            FAIL("receive should not be called");
            return 0;
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(socket.closed);
    CHECK(socket.opened);
    CHECK(buffer.size() == 0);
}

TEST_CASE("UdpRxQueueFiller available overflow")
{
    struct MockSocket
    {
        bool opened = false;
        bool closed = false;

        uint16_t available() const
        {
            return 65522;
        }

        bool open()
        {
            opened = true;
            return true;
        }

        void close()
        {
            closed = true;
        }

        bool reopen()
        {
            close();
            return open();
        }

        int32_t receive(uint8_t*, uint16_t, Endpoint&)
        {
            FAIL("receive should not be called");
            return 0;
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(socket.closed);
    CHECK(socket.opened);
    CHECK(buffer.size() == 0);
}

TEST_CASE("UdpRxQueueFiller receive error")
{
    struct MockSocket
    {
        bool consumed = false;

        uint16_t available() const
        {
            return consumed ? 0 : 64;
        }

        bool open()
        {
            return true;
        }

        void close()
        {
        }

        bool reopen()
        {
            close();
            return open();
        }

    int32_t receive(uint8_t*, uint16_t, Endpoint&)
        {
            consumed = true;
            return -1;
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(buffer.size() == 0);
}

TEST_CASE("UdpRxQueueFiller fifo desync")
{
    struct MockSocket
    {
        bool opened = false;
        bool closed = false;
        bool consumed = false;

        uint16_t available() const
        {
            return consumed ? 0 : 32;
        }

        bool open()
        {
            opened = true;
            return true;
        }

        void close()
        {
            closed = true;
        }

        bool reopen()
        {
            close();
            return open();
        }

        int32_t receive(uint8_t* data, uint16_t, Endpoint&)
        {
            Frame frame{};
            frame.size = 1;

            memcpy(data, &frame, sizeof(frame));

            consumed = true;

            return 64;
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(socket.closed);
    CHECK(socket.opened);
    CHECK(buffer.size() == 0);
}

TEST_CASE("UdpRxQueueFiller oversized packet")
{
    struct MockSocket
    {
        bool consumed = false;

        uint16_t available() const
        {
            return consumed
                ? 0
                : static_cast<uint16_t>(sizeof(Frame) + 32);
        }

        bool open()
        {
            return true;
        }

        void close()
        {
        }

        bool reopen()
        {
            close();
            return open();
        }
       
        int32_t receive(uint8_t* data, uint16_t, Endpoint&)
        {
            Frame frame{};
            frame.size = 1;

            memcpy(data, &frame, sizeof(frame));

            consumed = true;

            return static_cast<int32_t>(sizeof(Frame)) + 1;
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(buffer.size() == 0);
}TEST_CASE("UdpRxQueueFiller oversized packet")
{
    struct MockSocket
    {
        bool consumed = false;

        uint16_t available() const
        {
            return consumed
                ? 0
                : static_cast<uint16_t>(sizeof(Frame) + 32);
        }

        bool open()
        {
            return true;
        }

        void close()
        {
        }

        bool reopen()
        {
            close();
            return open();
        }
       
       int32_t receive(uint8_t* data, uint16_t, Endpoint&)
        {
            Frame frame{};
            frame.size = 1;

            memcpy(data, &frame, sizeof(frame));

            consumed = true;

            return static_cast<int32_t>(sizeof(Frame)) + 1;
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(buffer.size() == 0);
}

TEST_CASE("UdpRxQueueFiller frame size mismatch")
{
    struct MockSocket
    {
        bool consumed = false;

        uint16_t available() const
        {
            return consumed ? 0 : 64;
        }

        bool open()
        {
            return true;
        }

        void close()
        {
        }

        bool reopen()
        {
            close();
            return open();
        }
       
        int32_t receive(uint8_t* data, uint16_t, Endpoint&)
        {
            Frame frame{};
            frame.size = 4;

            memcpy(data, &frame, sizeof(frame));

            consumed = true;

            return static_cast<int32_t>(serialized_size(frame)) + 1;
        }
    };

    MockSocket socket;
    Buffer buffer;

    UdpRxQueueFiller<MockSocket, Buffer> filler(&socket, buffer);

    filler.fill();

    CHECK(buffer.size() == 0);
}

