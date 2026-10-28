#pragma once

#include <cstdint>
#include "Socket.hpp"

template<typename UdpSocket, typename Buffer>
class UdpRxQueueFiller
{
public:
    UdpRxQueueFiller(UdpSocket* socket, Buffer& buffer) : socket_(socket), buffer_(buffer)
    {}

    void fill()
    {
        while (socket_->poll())
        {
            Endpoint source;
        	auto& frame = buffer_.begin_write();
            socket_->receive(reinterpret_cast<uint8_t*>(&frame), sizeof(frame), source);
            buffer_.commit_write();
        }
    }

private:
    UdpSocket* socket_;
    Buffer& buffer_;
};
