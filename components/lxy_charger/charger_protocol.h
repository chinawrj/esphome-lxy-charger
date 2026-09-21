#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace esphome { namespace lxy_charger { namespace protocol {
constexpr size_t MaxFrameSize = 258;

inline uint16_t readU16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

inline size_t encode(uint8_t command, const uint8_t* payload,
                     size_t payloadSize, uint8_t* output) {
    if (payloadSize > 253) return 0;
    output[0] = output[1] = 0x5E;
    output[2] = static_cast<uint8_t>(payloadSize + 2);
    output[3] = command;
    uint8_t checksum = output[2] ^ command;
    for (size_t i = 0; i < payloadSize; ++i) {
        output[4 + i] = payload[i];
        checksum ^= payload[i];
    }
    output[4 + payloadSize] = checksum;
    return payloadSize + 5;
}

inline size_t encodeSet(uint16_t voltage, uint16_t current, uint8_t* out) {
    const uint8_t data[] = {0x01, static_cast<uint8_t>(voltage >> 8),
        static_cast<uint8_t>(voltage), static_cast<uint8_t>(current >> 8),
        static_cast<uint8_t>(current)};
    return encode(0x03, data, sizeof(data), out);
}

// Accept arbitrary notification boundaries: one frame may be split, and one
// notification may contain several frames. Invalid input resynchronizes at 5E5E.
class Decoder {
public:
    void reset() { used_ = 0; }

    template <typename Handler>
    void feed(const uint8_t* data, size_t length, Handler handler) {
        for (size_t i = 0; i < length; ++i) {
            if (used_ == sizeof(buffer_)) discard(1);
            buffer_[used_++] = data[i];
            while (used_ >= 2) {
                if (buffer_[0] != 0x5E || buffer_[1] != 0x5E) {
                    discard(1);
                    continue;
                }
                if (used_ < 3) break;
                if (buffer_[2] < 2) { discard(1); continue; }
                const size_t total = static_cast<size_t>(buffer_[2]) + 3;
                if (used_ < total) break;
                uint8_t checksum = 0;
                for (size_t j = 2; j < total; ++j) checksum ^= buffer_[j];
                if (checksum == 0) {
                    // Consume before callback: a rejected reply may reset the
                    // connection and reset this decoder from inside handler.
                    uint8_t complete[MaxFrameSize];
                    memcpy(complete, buffer_, total);
                    discard(total);
                    handler(complete, total);
                } else {
                    ++checksumErrors;
                    discard(1);
                }
            }
        }
    }

    uint32_t checksumErrors = 0;

private:
    void discard(size_t count) {
        used_ -= count;
        memmove(buffer_, buffer_ + count, used_);
    }
    uint8_t buffer_[MaxFrameSize]{};
    size_t used_ = 0;
};
}
} }
