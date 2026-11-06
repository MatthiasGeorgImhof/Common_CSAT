#pragma once

#include <cstdint>
#include "Socket.hpp"

#include "Logger.hpp"

template<typename UdpSocket, typename Buffer>
class UdpRxQueueFiller
{
public:
    UdpRxQueueFiller(UdpSocket* socket, Buffer& buffer) : socket_(socket), buffer_(buffer)
    {}

    void fill()
    {
        while (auto available = socket_->available())
        {
            // 1. Hardware pointer underflow detection:
            // W5500 socket buffer is max 2048 bytes. Anything > 2048 means
            // Sn_RX_RD overshot Sn_RX_WR and underflowed (e.g. 65522).
            if (available > 2048 || available < 8)
            {
                log(LOG_LEVEL_ERROR, "UdpRxQueueFiller: hardware pointer underflow (%u), resetting socket\r\n", available);
                socket_->reopen();
                break;
            }

            Endpoint source;
            auto& frame = buffer_.begin_write();

            int32_t len = socket_->receive(reinterpret_cast<uint8_t*>(&frame), sizeof(frame), source);

            // 2. Basic I/O error
            if (len <= 0)
            {
                log(LOG_LEVEL_ERROR, "UdpRxQueueFiller: receive error %d\r\n", len);
                continue;
            }

            // 3. FIFO space check (MUST be: len + 8 <= available)
            // If this fails, recvfrom read garbage from the header and overshot the FIFO.
            if ((len + 8) > available)
            {
                log(LOG_LEVEL_ERROR, "UdpRxQueueFiller: packet len (%d + 8) > available (%d), desync detected!\r\n", len, available);
                socket_->reopen();
                break;
            }

            // 4. Buffer overflow protection
            if (len > static_cast<int32_t>(sizeof(frame)))
            {
                log(LOG_LEVEL_ERROR, "UdpRxQueueFiller: packet len %d > buffer MTU %d\r\n", len, sizeof(frame));
                break;
            }

            // 5. Wire protocol validation: len MUST match the Cyphal HeaderFrame framing
            if (len != static_cast<int32_t>(serialized_size(frame)))
            {
                log(LOG_LEVEL_ERROR, "UdpRxQueueFiller: frame size mismatch: wire len %d != frame %d + 6\r\n", len, frame.size);
                // Drop this frame, do NOT commit!
                continue;
            }

            // ALL CHECKS PASSED: Frame is 100% valid!
            log(LOG_LEVEL_DEBUG, "UdpRxQueueFiller: valid frame %d bytes (len %d, avail %d)\r\n", frame.size, len, available);
            buffer_.commit_write();
        }
    }

private:
    UdpSocket* socket_;
    Buffer& buffer_;
};
