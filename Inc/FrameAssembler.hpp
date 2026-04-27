#pragma once
#include "CC1101Manager.hpp"
#include "MessageAccumulator.hpp"
#include <cstring>

template <typename RadioBuffer, typename SerialBuffer>
class FrameAssembler {
private:
    RadioBuffer& radio_rx_buffer_;
    SerialBuffer& serial_out_buffer_;
    MessageAccumulator<1024> accumulator_;

    // Static callback used by the accumulator
    static bool on_frame_completed(void* user_ref, size_t len, const uint8_t* data) {
        auto* self = static_cast<FrameAssembler*>(user_ref);
        
        // 1. Reserve slot in the Serial Buffer
        if (self->serial_out_buffer_.is_full()) return false;
        
        SerialFrame& slot = self->serial_out_buffer_.begin_write();
        
        // 2. Copy the reassembled frame
        size_t copy_len = (len > SERIAL_MTU) ? SERIAL_MTU : len;
        slot.size = copy_len;
        std::memcpy(slot.data, data, copy_len);
        
        // 3. Commit to the Serial Buffer
        self->serial_out_buffer_.commit_write();
        return true;
    }

public:
    FrameAssembler(RadioBuffer& radio_rx, SerialBuffer& serial_out)
        : radio_rx_buffer_(radio_rx), 
          serial_out_buffer_(serial_out),
          accumulator_(this, on_frame_completed) {}

    void process() {
        // Drain all available radio packets
        size_t num_packets = radio_rx_buffer_.size();
        for (size_t n = 0; n < num_packets; ++n) {
            CC1101Packet packet = radio_rx_buffer_.pop();
            CC1101PacketView view(packet.data());
            
            uint8_t* data = view.payload();
            uint8_t len = view.len();

            // Feed byte-by-byte into the accumulator
            for (uint8_t i = 0; i < len; ++i) {
                accumulator_.process(data[i]);
            }
        }
    }
};