#pragma once

#include <cstdint>
#include <algorithm>

template<size_t MaxSize>
class MessageAccumulator {
private:
    uint8_t buf[MaxSize];
    size_t len = 0;
    bool in_message = false;
    void* user_ref;
    bool (*emit_func)(void*, size_t, const uint8_t*);

public:
    MessageAccumulator(void* user_reference, bool (*emit)(void*, size_t, const uint8_t*)) 
        : user_ref(user_reference), emit_func(emit) {}

    void set_user_reference(void* new_ref) { user_ref = new_ref; }

    void process(uint8_t b) {
        if (!in_message) {
            // Waiting for start marker
            if (b == 0x00) {
                in_message = true;
                len = 0;
                buf[len++] = b;
            }
            return;
        }

        // We are in a message. Add the byte.
        buf[len++] = b;

        // Condition 1: End marker found
        if (b == 0x00 && len > 1) {
            emit_func(user_ref, len, buf);
            in_message = false;
            len = 0;
            return;
        }

        // Condition 2: Buffer full (Force Emit)
        if (len == MaxSize) {
            emit_func(user_ref, len, buf);
            in_message = false;
            len = 0;
        }
    }
};