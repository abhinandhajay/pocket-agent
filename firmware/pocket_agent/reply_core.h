#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace badgereply {
class Receiver {
  public:
    char text[4097]{};
    size_t textBytes = 0, bytes = 0, sequence = 0;
    bool receiving = false, complete = false;
    bool begin(size_t t) {
        if (!t || t > 4096)
            return false;
        text[0] = 0;
        textBytes = t;
        bytes = sequence = 0;
        receiving = true;
        complete = false;
        return true;
    }
    bool append(size_t seq, const uint8_t *data, size_t count) {
        if (!receiving || !data || bytes > textBytes || seq != sequence ||
            count != (textBytes - bytes < 384 ? textBytes - bytes : 384) || !count)
            return false;
        for (size_t i = 0; i < count; i++)
            if (data[i] != '\n' && (data[i] < 32 || data[i] > 126))
                return false;
        std::memcpy(text + bytes, data, count);
        bytes += count;
        sequence++;
        if (bytes >= textBytes)
            text[textBytes] = 0;
        return true;
    }
    bool end(size_t total, bool digestMatches) {
        if (!receiving || total != bytes || bytes != textBytes || !digestMatches)
            return false;
        receiving = false;
        complete = true;
        return true;
    }
    bool textReady() const { return bytes >= textBytes && textBytes > 0; }
    void clear() {
        text[0] = 0;
        textBytes = bytes = sequence = 0;
        receiving = complete = false;
    }
};
} // namespace badgereply
