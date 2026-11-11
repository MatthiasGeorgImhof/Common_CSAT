#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "MessageAccumulator.hpp"
#include <vector>
#include <algorithm>

static std::vector<std::vector<uint8_t>> emitted_messages;

static bool mock_emit(void* /*user_ref*/, size_t data_size, const uint8_t* data) {
    emitted_messages.emplace_back(data, data + data_size);
    return true;
}

static void reset_emitted() {
    emitted_messages.clear();
}

TEST_CASE("MessageAccumulator with MaxSize 255 (UART-like)") {
    reset_emitted();
    MessageAccumulator<255> accumulator(nullptr, mock_emit);

    SUBCASE("Single message with end marker") {
        std::vector<uint8_t> input = {0x00, 0x01, 0x02, 0x03, 0x00};
        for (uint8_t b : input) {
            accumulator.process(b);
        }
        REQUIRE(emitted_messages.size() == 1);
        CHECK(emitted_messages[0] == input);
    }

    SUBCASE("Message reaching max size without end marker") {
        std::vector<uint8_t> input(256, 0x01); // 256 bytes, but max 255
        input[0] = 0x00; // start
        for (uint8_t b : input) {
            accumulator.process(b);
        }
        REQUIRE(emitted_messages.size() == 1);
        CHECK(emitted_messages[0].size() == 255);
        CHECK(emitted_messages[0][0] == 0x00);
        for (size_t i = 1; i < emitted_messages[0].size(); ++i) {
            CHECK(emitted_messages[0][i] == 0x01);
        }
    }

    SUBCASE("Multiple messages") {
        std::vector<uint8_t> input1 = {0x00, 0x01, 0x02, 0x00};
        std::vector<uint8_t> input2 = {0x00, 0x03, 0x04, 0x00};
        for (uint8_t b : input1) accumulator.process(b);
        for (uint8_t b : input2) accumulator.process(b);
        REQUIRE(emitted_messages.size() == 2);
        CHECK(emitted_messages[0] == input1);
        CHECK(emitted_messages[1] == input2);
    }

    SUBCASE("Data before start marker ignored") {
        std::vector<uint8_t> input = {0x01, 0x02, 0x00, 0x03, 0x04, 0x00};
        for (uint8_t b : input) {
            accumulator.process(b);
        }
        REQUIRE(emitted_messages.size() == 1);
        CHECK(emitted_messages[0] == std::vector<uint8_t>{0x00, 0x03, 0x04, 0x00});
    }
}

TEST_CASE("MessageAccumulator with MaxSize 51 (Radio-like)") {
    reset_emitted();
    MessageAccumulator<51> accumulator(nullptr, mock_emit);

        SUBCASE("Single message with end marker") {
        std::vector<uint8_t> input = {0x00, 0x01, 0x02, 0x03, 0x00};
        for (uint8_t b : input) {
            accumulator.process(b);
        }
        REQUIRE(emitted_messages.size() == 1);
        CHECK(emitted_messages[0] == input);
    }

    SUBCASE("Message reaching max size 51") {
        std::vector<uint8_t> input(52, 0x01); // 52 bytes
        input[0] = 0x00;
        for (uint8_t b : input) {
            accumulator.process(b);
        }
        REQUIRE(emitted_messages.size() == 1);
        CHECK(emitted_messages[0].size() == 51);
        CHECK(emitted_messages[0][0] == 0x00);
        for (size_t i = 1; i < emitted_messages[0].size(); ++i) {
            CHECK(emitted_messages[0][i] == 0x01);
        }
    }
}