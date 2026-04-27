#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <vector>
#include <string>
#include <sstream>
#include <cstring>
#include <array>

// --- Minimal Mocks for types used in FrameAssembler ---

// using CC1101Packet = std::array<std::uint8_t, 64>;

// struct CC1101PacketView {
//     uint8_t* data;
//     explicit CC1101PacketView(uint8_t* ptr) : data(ptr) {}
//     uint8_t  len() const { return data[0]; }
//     void     set_len(uint8_t v) { data[0] = v; }
//     uint8_t* payload() { return &data[1]; }
// };

constexpr size_t SERIAL_MTU = 640;
struct SerialFrame {
    size_t size;
    uint8_t data[SERIAL_MTU];
};

// Simplified CircularBuffer for Testing
template<typename T, size_t Size>
class MockBuffer {
    std::vector<T> storage;
public:
    void push(const T& item) { storage.push_back(item); }
    T pop() { 
        T item = storage.front(); 
        storage.erase(storage.begin()); 
        return item; 
    }
    size_t size() const { return storage.size(); }
    bool is_empty() const { return storage.empty(); }
    bool is_full() const { return storage.size() >= Size; }
    
    // begin_write/commit_write simulation
    T temp_item;
    T& begin_write() { return temp_item; }
    void commit_write() { push(temp_item); }
};

// --- The Classes to Test ---
#include "MessageAccumulator.hpp"
#include "FrameAssembler.hpp" // Assuming the class we wrote is here

// Utility
std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::stringstream ss(hex);
    std::string byteString;
    while (ss >> byteString) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(byteString, nullptr, 16)));
    }
    return bytes;
}

TEST_CASE("FrameAssembler - Immediate Emit Logic") {
    MockBuffer<CC1101Packet, 10> radio_rx;
    MockBuffer<SerialFrame, 10> serial_out;
    FrameAssembler<MockBuffer<CC1101Packet, 10>, MockBuffer<SerialFrame, 10>> assembler(radio_rx, serial_out);

    SUBCASE("Immediate completion (Latency Test)") {
        // We send one packet containing a full frame. 
        // It should be available in serial_out IMMEDIATELY.
        CC1101Packet pkt;
        CC1101PacketView view(pkt.data());
        uint8_t data[] = {0x00, 0x11, 0x22, 0x00};
        view.set_len(4);
        memcpy(view.payload(), data, 4);
        
        radio_rx.push(pkt);
        assembler.process();

        // Verification: The frame is emitted without waiting for a second 0x00
        REQUIRE(serial_out.size() == 1);
        CHECK(serial_out.pop().size == 4);
    }

    SUBCASE("Back-to-back frames (Double Zero 00 00)") {
        // Stream: [00 AA 00] [00 BB 00]
        // Bytes: 00 AA 00 00 BB 00
        CC1101Packet pkt;
        CC1101PacketView view(pkt.data());
        uint8_t stream[] = {0x00, 0xAA, 0x00, 0x00, 0xBB, 0x00};
        view.set_len(6);
        memcpy(view.payload(), stream, 6);
        
        radio_rx.push(pkt);
        assembler.process();

        REQUIRE(serial_out.size() == 2);
        
        SerialFrame f1 = serial_out.pop();
        CHECK(f1.size == 3);
        CHECK(f1.data[1] == 0xAA);

        SerialFrame f2 = serial_out.pop();
        CHECK(f2.size == 3);
        CHECK(f2.data[1] == 0xBB);
    }

    SUBCASE("Triple Zero Handling (Back-to-back empty frames)") {
        // Stream: 00 00 00
        // Byte 1: Start
        // Byte 2: End (Emit 00 00)
        // Byte 3: Start
        CC1101Packet pkt;
        CC1101PacketView view(pkt.data());
        uint8_t stream[] = {0x00, 0x00, 0x00};
        view.set_len(3);
        memcpy(view.payload(), stream, 3);
        
        radio_rx.push(pkt);
        assembler.process();

        // Should emit one [00 00] frame and be in the middle of starting the next
        CHECK(serial_out.size() == 1);
        CHECK(serial_out.pop().size == 2);
    }
    
    SUBCASE("Noise robustness") {
        // Stream: FF EE 00 AA BB 00
        // FF, EE ignored (IDLE)
        // 00 starts, collects AA BB 00, emits.
        CC1101Packet pkt;
        CC1101PacketView view(pkt.data());
        uint8_t stream[] = {0xFF, 0xEE, 0x00, 0xAA, 0xBB, 0x00};
        view.set_len(6);
        memcpy(view.payload(), stream, 6);
        
        radio_rx.push(pkt);
        assembler.process();

        REQUIRE(serial_out.size() == 1);
        CHECK(serial_out.pop().size == 4);
    }
}