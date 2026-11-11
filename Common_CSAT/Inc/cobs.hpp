#pragma once

#include <cstddef>
#include <cstdint>

namespace cobs
{

// This in-place representation has one external code byte and an encoded
// region having exactly the same size as the original data.
//
// Limiting the region to 253 bytes avoids the special 0xFF COBS case,
// which can require another output byte.
constexpr std::size_t max_in_place_size = 253;


// Encodes data in place and returns the first COBS code.
//
// Example:
//
//     Input data:       11 22 00 33 00 44
//     Returned code:    03
//     Encoded data:     11 22 02 33 02 44
//
// Complete COBS representation:
//
//     03 11 22 02 33 02 44
//
inline uint8_t encode_in_place(uint8_t* const data, const std::size_t size)
{
    if (size == 0)
    {
        return 1;
    }

    // The caller must ensure this.
    if ((data == nullptr) || (size > max_in_place_size))
    {
        return 0;
    }

    uint8_t first_code = 0;

    // Index at which the current block begins.
    std::size_t block_begin = 0;

    // Location of the zero that will hold the next block's code.
    // This value is meaningful only after the first zero is found.
    std::size_t previous_zero = 0;
    bool found_zero = false;

    for (std::size_t index = 0; index < size; ++index)
    {
        if (data[index] != 0)
        {
            continue;
        }

        const auto code = static_cast<uint8_t>(
            index - block_begin + 1);

        if (!found_zero)
        {
            first_code = code;
            found_zero = true;
        }
        else
        {
            data[previous_zero] = code;
        }

        previous_zero = index;
        block_begin = index + 1;
    }

    const auto final_code = static_cast<uint8_t>(
        size - block_begin + 1);

    if (!found_zero)
    {
        // The entire buffer is one nonzero block.
        first_code = final_code;
    }
    else
    {
        // Replace the final zero with the code for the final block.
        // If the original data ended in zero, final_code is 1.
        data[previous_zero] = final_code;
    }

    return first_code;
}


// Decodes data in place using the separately stored first COBS code.
//
// Returns false if the encoding is malformed.
//
// Important: If malformed input is discovered after one or more blocks,
// the buffer may already have been partially decoded.
//
inline bool decode_in_place(const uint8_t first_code, uint8_t* const data, const std::size_t size)
{
    if (size == 0)
    {
        return first_code == 1;
    }

    if ((data == nullptr) || (size > max_in_place_size) || (first_code == 0) || (first_code == 0xFF))
    {
        return false;
    }

    uint8_t code = first_code;
    std::size_t block_begin = 0;

    while (true)
    {
        // The code value includes the code byte itself. Because the first
        // code is external, the zero/code position in data[] is code - 1
        // bytes from the beginning of the current block.
        const std::size_t next_code_position = block_begin + static_cast<std::size_t>(code) - 1;

        if (next_code_position > size)
        {
            // The block extends beyond the encoded data.
            return false;
        }

        if (next_code_position == size)
        {
            // The final block ends exactly at the end of the buffer.
            return true;
        }

        // Save the next code before restoring this location to zero.
        const uint8_t next_code = data[next_code_position];

        if ((next_code == 0) || (next_code == 0xFF))
        {
            return false;
        }

        data[next_code_position] = 0;

        code = next_code;
        block_begin = next_code_position + 1;
    }
}

} // namespace cobs