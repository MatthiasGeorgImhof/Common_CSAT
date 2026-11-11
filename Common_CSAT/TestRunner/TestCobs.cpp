#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "cobs.hpp"


namespace
{

// -----------------------------------------------------------------------------
// Test helpers
// -----------------------------------------------------------------------------

template <std::size_t N>
void check_round_trip(const std::array<uint8_t, N>& input)
{
    auto encoded = input;

    const uint8_t first_code = cobs::encode_in_place(encoded.data(), encoded.size());

    REQUIRE(first_code != 0);

    auto decoded = encoded;

    REQUIRE(cobs::decode_in_place(first_code, decoded.data(), decoded.size()));

    CHECK(decoded == input);
}


void check_round_trip(const std::vector<uint8_t>& input)
{
    auto encoded = input;

    auto* data = encoded.empty() ? nullptr : encoded.data();

    const uint8_t first_code = cobs::encode_in_place(data, encoded.size());

    REQUIRE(first_code != 0);

    auto decoded = encoded;

    data = decoded.empty() ? nullptr : decoded.data();

    REQUIRE(cobs::decode_in_place(first_code, data, decoded.size()));

    CHECK(decoded == input);
}


// Produces the complete conventional COBS output:
//
//     [first code][encoded data]
//
// This is useful for comparing the specialized in-place representation with
// a conventional COBS implementation.
std::vector<uint8_t> canonical_cobs_encode(const uint8_t* const input, const std::size_t size)
{
    std::vector<uint8_t> output;

    // For buffers limited to 253 bytes, size + 1 is sufficient.
    output.reserve(size + 1);

    std::size_t code_position = 0;
    uint8_t code = 1;

    output.push_back(0);

    for (std::size_t index = 0; index < size; ++index)
    {
        if (input[index] == 0)
        {
            output[code_position] = code;

            code_position = output.size();
            output.push_back(0);
            code = 1;
        }
        else
        {
            output.push_back(input[index]);
            ++code;
        }
    }

    output[code_position] = code;

    return output;
}


std::vector<uint8_t> in_place_cobs_encode(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> encoded = input;

    const uint8_t first_code = cobs::encode_in_place(encoded.empty() ? nullptr : encoded.data(), encoded.size());

    if (first_code == 0)
    {
        return {};
    }

    std::vector<uint8_t> complete_encoding;
    complete_encoding.reserve(encoded.size() + 1);

    complete_encoding.push_back(first_code);
    complete_encoding.insert(complete_encoding.end(), encoded.begin(), encoded.end());

    return complete_encoding;
}

} // namespace


// =============================================================================
// Empty input
// =============================================================================

TEST_CASE("COBS empty input")
{
    CHECK(cobs::encode_in_place(nullptr, 0) == 1);
    CHECK(cobs::decode_in_place(1, nullptr, 0));

    CHECK_FALSE(cobs::decode_in_place(0, nullptr, 0));
    CHECK_FALSE(cobs::decode_in_place(2, nullptr, 0));
}


// =============================================================================
// Known encoding vectors
// =============================================================================

TEST_CASE("COBS encodes one nonzero byte")
{
    std::array<uint8_t, 1> data {0x11};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 2);
    CHECK(data == std::array<uint8_t, 1> {0x11});
}


TEST_CASE("COBS encodes one zero byte")
{
    std::array<uint8_t, 1> data {0x00};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 1);
    CHECK(data == std::array<uint8_t, 1> {0x01});
}


TEST_CASE("COBS encodes data without zeros")
{
    std::array<uint8_t, 4> data {0x11, 0x22, 0x33, 0x44};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 5);

    CHECK(data == std::array<uint8_t, 4> {
        0x11, 0x22, 0x33, 0x44
    });
}


TEST_CASE("COBS encodes an embedded zero")
{
    std::array<uint8_t, 5> data {0x11, 0x22, 0x00, 0x33, 0x44};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 3);

    CHECK(data == std::array<uint8_t, 5> {
        0x11, 0x22, 0x03, 0x33, 0x44
    });
}


TEST_CASE("COBS encodes multiple embedded zeros")
{
    std::array<uint8_t, 6> data {0x11, 0x22, 0x00, 0x33, 0x00, 0x44};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 3);

    CHECK(data == std::array<uint8_t, 6> {
        0x11, 0x22, 0x02, 0x33, 0x02, 0x44
    });
}


TEST_CASE("COBS encodes a leading zero")
{
    std::array<uint8_t, 3> data {0x00, 0x11, 0x22};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 1);

    CHECK(data == std::array<uint8_t, 3> {
        0x03, 0x11, 0x22
    });
}


TEST_CASE("COBS encodes a trailing zero")
{
    std::array<uint8_t, 3> data {0x11, 0x22, 0x00};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 3);

    CHECK(data == std::array<uint8_t, 3> {
        0x11, 0x22, 0x01
    });
}


TEST_CASE("COBS encodes consecutive zeros")
{
    std::array<uint8_t, 6> data {0x11, 0x00, 0x00, 0x00, 0x22, 0x33};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 2);

    CHECK(data == std::array<uint8_t, 6> {
        0x11, 0x01, 0x01, 0x03, 0x22, 0x33
    });
}


TEST_CASE("COBS encodes an all-zero buffer")
{
    std::array<uint8_t, 5> data {0x00, 0x00, 0x00, 0x00, 0x00};

    const uint8_t code = cobs::encode_in_place(data.data(), data.size());

    CHECK(code == 1);

    CHECK(data == std::array<uint8_t, 5> {
        0x01, 0x01, 0x01, 0x01, 0x01
    });
}


// =============================================================================
// Known decoding vectors
// =============================================================================

TEST_CASE("COBS decodes data without zeros")
{
    std::array<uint8_t, 4> data {0x11, 0x22, 0x33, 0x44};

    REQUIRE(cobs::decode_in_place(5, data.data(), data.size()));

    CHECK(data == std::array<uint8_t, 4> {
        0x11, 0x22, 0x33, 0x44
    });
}


TEST_CASE("COBS decodes an embedded zero")
{
    std::array<uint8_t, 5> data {0x11, 0x22, 0x03, 0x33, 0x44};

    REQUIRE(cobs::decode_in_place(3, data.data(), data.size()));

    CHECK(data == std::array<uint8_t, 5> {
        0x11, 0x22, 0x00, 0x33, 0x44
    });
}


TEST_CASE("COBS decodes multiple embedded zeros")
{
    std::array<uint8_t, 6> data {0x11, 0x22, 0x02, 0x33, 0x02, 0x44};

    REQUIRE(cobs::decode_in_place(3, data.data(), data.size()));

    CHECK(data == std::array<uint8_t, 6> {
        0x11, 0x22, 0x00, 0x33, 0x00, 0x44
    });
}


TEST_CASE("COBS decodes a leading zero")
{
    std::array<uint8_t, 3> data {0x03, 0x11, 0x22};

    REQUIRE(cobs::decode_in_place(1, data.data(), data.size()));

    CHECK(data == std::array<uint8_t, 3> {
        0x00, 0x11, 0x22
    });
}


TEST_CASE("COBS decodes a trailing zero")
{
    std::array<uint8_t, 3> data {0x11, 0x22, 0x01};

    REQUIRE(cobs::decode_in_place(3, data.data(), data.size()));

    CHECK(data == std::array<uint8_t, 3> {
        0x11, 0x22, 0x00
    });
}


TEST_CASE("COBS decodes consecutive zeros")
{
    std::array<uint8_t, 6> data {0x11, 0x01, 0x01, 0x03, 0x22, 0x33};

    REQUIRE(cobs::decode_in_place(2, data.data(), data.size()));

    CHECK(data == std::array<uint8_t, 6> {
        0x11, 0x00, 0x00, 0x00, 0x22, 0x33
    });
}


TEST_CASE("COBS decodes an all-zero original buffer")
{
    std::array<uint8_t, 5> data {0x01, 0x01, 0x01, 0x01, 0x01};

    REQUIRE(cobs::decode_in_place(1, data.data(), data.size()));

    CHECK(data == std::array<uint8_t, 5> {
        0x00, 0x00, 0x00, 0x00, 0x00
    });
}


// =============================================================================
// Round-trip testing
// =============================================================================

TEST_CASE("COBS round-trips representative buffers")
{
    check_round_trip(std::array<uint8_t, 1> {0x00});

    check_round_trip(std::array<uint8_t, 1> {0xA5});

    check_round_trip(std::array<uint8_t, 8> {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});

    check_round_trip(std::array<uint8_t, 12> {0x00, 0x00, 0x00, 0x00, 0x10, 0x20, 0x00, 0x30, 0x40, 0x00, 0x50, 0x00});
}


TEST_CASE("COBS round-trips every possible single-byte value")
{
    for (unsigned value = 0; value <= 0xFF; ++value)
    {
        CAPTURE(value);

        std::array<uint8_t, 1> input {static_cast<uint8_t>(value)};

        check_round_trip(input);
    }
}


TEST_CASE("COBS round-trips every possible two-byte value")
{
    for (unsigned first = 0; first <= 0xFF; ++first)
    {
        for (unsigned second = 0; second <= 0xFF; ++second)
        {
            CAPTURE(first);
            CAPTURE(second);

            std::array<uint8_t, 2> input {
                static_cast<uint8_t>(first),
                static_cast<uint8_t>(second)
            };

            check_round_trip(input);
        }
    }
}


TEST_CASE("COBS round-trips all zero placement patterns for 12 bytes")
{
    constexpr std::size_t size = 12;
    constexpr std::size_t pattern_count = 1U << size;

    for (std::size_t pattern = 0;
         pattern < pattern_count;
         ++pattern)
    {
        CAPTURE(pattern);

        std::array<uint8_t, size> input {};

        for (std::size_t index = 0; index < size; ++index)
        {
            const bool zero = (pattern & (std::size_t {1} << index)) != 0;

            input[index] = zero
                ? 0x00
                : static_cast<uint8_t>(index + 1);
        }

        check_round_trip(input);
    }
}


TEST_CASE("COBS round-trips randomized buffers")
{
    // Fixed seed makes failures reproducible.
    std::mt19937 generator {0xC0B51234U};

    std::uniform_int_distribution<unsigned> byte_distribution {
        0,
        0xFF
    };

    for (std::size_t size = 0;
         size <= cobs::max_in_place_size;
         ++size)
    {
        for (unsigned iteration = 0;
             iteration < 100;
             ++iteration)
        {
            CAPTURE(size);
            CAPTURE(iteration);

            std::vector<uint8_t> input(size);

            for (auto& byte : input)
            {
                byte = static_cast<uint8_t>(
                    byte_distribution(generator));
            }

            check_round_trip(input);
        }
    }
}


// =============================================================================
// Comparison against a conventional COBS encoder
// =============================================================================

TEST_CASE("In-place COBS matches canonical COBS known vectors")
{
    const std::vector<std::vector<uint8_t>> vectors {
        {},
        {0x00},
        {0x11},
        {0x11, 0x22},
        {0x11, 0x00, 0x22},
        {0x00, 0x11, 0x22},
        {0x11, 0x22, 0x00},
        {0x00, 0x00, 0x00},
        {0x11, 0x00, 0x00, 0x22},
        {0x11, 0x22, 0x00, 0x33, 0x00, 0x44}
    };

    for (const auto& input : vectors)
    {
        const auto expected = canonical_cobs_encode(input.empty() ? nullptr : input.data(), input.size());

        const auto actual = in_place_cobs_encode(input);

        CHECK(actual == expected);
    }
}


TEST_CASE("In-place COBS matches canonical COBS for random buffers")
{
    std::mt19937 generator {0x51A7C0B5U};

    std::uniform_int_distribution<unsigned> byte_distribution {
        0,
        0xFF
    };

    for (std::size_t size = 0;
         size <= cobs::max_in_place_size;
         ++size)
    {
        for (unsigned iteration = 0;
             iteration < 100;
             ++iteration)
        {
            CAPTURE(size);
            CAPTURE(iteration);

            std::vector<uint8_t> input(size);

            for (auto& byte : input)
            {
                byte = static_cast<uint8_t>(
                    byte_distribution(generator));
            }

            const auto expected = canonical_cobs_encode(input.empty() ? nullptr : input.data(), input.size());

            const auto actual = in_place_cobs_encode(input);

            CHECK(actual == expected);
        }
    }
}


// =============================================================================
// Boundary conditions
// =============================================================================

TEST_CASE("COBS handles maximum in-place buffer without zeros")
{
    std::vector<uint8_t> input(
        cobs::max_in_place_size,
        0xA5);

    auto encoded = input;

    const uint8_t first_code = cobs::encode_in_place(encoded.data(), encoded.size());

    CHECK(first_code == 0xFE);
    CHECK(encoded == input);

    REQUIRE(cobs::decode_in_place(first_code, encoded.data(), encoded.size()));

    CHECK(encoded == input);
}


TEST_CASE("COBS handles maximum in-place buffer with zeros")
{
    std::vector<uint8_t> input(
        cobs::max_in_place_size,
        0xA5);

    input.front() = 0x00;
    input[input.size() / 2] = 0x00;
    input.back() = 0x00;

    check_round_trip(input);
}


TEST_CASE("COBS rejects oversized input")
{
    std::vector<uint8_t> data(
        cobs::max_in_place_size + 1,
        0xA5);

    CHECK(cobs::encode_in_place(data.data(), data.size()) == 0);

    CHECK_FALSE(cobs::decode_in_place(1, data.data(), data.size()));
}


TEST_CASE("COBS rejects null storage for nonempty input")
{
    CHECK(cobs::encode_in_place(nullptr, 1) == 0);
    CHECK_FALSE(cobs::decode_in_place(1, nullptr, 1));
}


// =============================================================================
// Malformed encodings
// =============================================================================

TEST_CASE("COBS rejects a zero first code")
{
    std::array<uint8_t, 3> data {0x11, 0x22, 0x33};

    CHECK_FALSE(cobs::decode_in_place(0, data.data(), data.size()));
}


TEST_CASE("COBS rejects reserved 0xFF first code")
{
    std::array<uint8_t, 3> data {0x11, 0x22, 0x33};

    CHECK_FALSE(cobs::decode_in_place(0xFF, data.data(), data.size()));
}


TEST_CASE("COBS rejects a first code beyond the buffer")
{
    std::array<uint8_t, 3> data {0x11, 0x22, 0x33};

    CHECK_FALSE(cobs::decode_in_place(5, data.data(), data.size()));
}


TEST_CASE("COBS rejects a zero subsequent code")
{
    std::array<uint8_t, 4> data {0x11, 0x00, 0x22, 0x33};

    CHECK_FALSE(cobs::decode_in_place(2, data.data(), data.size()));
}


TEST_CASE("COBS rejects a reserved 0xFF subsequent code")
{
    std::array<uint8_t, 4> data {0x11, 0xFF, 0x22, 0x33};

    CHECK_FALSE(cobs::decode_in_place(2, data.data(), data.size()));
}


TEST_CASE("COBS rejects a subsequent code beyond the buffer")
{
    std::array<uint8_t, 4> data {0x11, 0x05, 0x22, 0x33};

    CHECK_FALSE(cobs::decode_in_place(2, data.data(), data.size()));
}


// =============================================================================
// HeaderFrame integration
// =============================================================================

template <std::size_t N>
struct __attribute__((packed)) HeaderFrame
{
    uint32_t extended_can_id;
    uint8_t payload[N];
};


static_assert(
    sizeof(HeaderFrame<8>) == 12, "HeaderFrame<8> should occupy exactly 12 bytes");


TEST_CASE("COBS round-trips HeaderFrame with zero bytes")
{
    HeaderFrame<8> original {};

    original.extended_can_id = 0x00123400;

    const std::array<uint8_t, 8> payload {0x00, 0x11, 0x22, 0x00, 0x33, 0x44, 0x00, 0x55};

    std::memcpy(original.payload, payload.data(), payload.size());

    HeaderFrame<8> encoded = original;

    auto* encoded_bytes = reinterpret_cast<uint8_t*>(&encoded);

    const uint8_t first_code = cobs::encode_in_place(encoded_bytes, sizeof(encoded));

    REQUIRE(first_code != 0);

    REQUIRE(cobs::decode_in_place(first_code, encoded_bytes, sizeof(encoded)));

    CHECK(std::memcmp(encoded.payload, original.payload, sizeof(original.payload)) == 0);

    CHECK(std::memcmp(&encoded, &original, sizeof(original)) == 0);
}


TEST_CASE("Encoded HeaderFrame contains no zero bytes")
{
    HeaderFrame<8> frame {};

    frame.extended_can_id = 0x00123400;

    const std::array<uint8_t, 8> payload {0x00, 0x11, 0x22, 0x00, 0x33, 0x44, 0x00, 0x55};

    std::memcpy(frame.payload, payload.data(), payload.size());

    auto* bytes = reinterpret_cast<uint8_t*>(&frame);

    const uint8_t first_code = cobs::encode_in_place(bytes, sizeof(frame));

    REQUIRE(first_code != 0);

    for (std::size_t index = 0;
         index < sizeof(frame);
         ++index)
    {
        CAPTURE(index);
        CHECK(bytes[index] != 0);
    }
}