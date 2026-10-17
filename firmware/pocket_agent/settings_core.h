#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace badgesettings {
constexpr int kRows = 6, kSound = 4, kBrightness = 5;
constexpr int kLeft = 68, kRight = 400, kTop = 115, kPitch = 38, kHeight = 34;
inline bool validBrightness(int value) {
    return value == 64 || value == 128 || value == 192 || value == 255;
}
inline int percent(int value) { return (value * 100 + 127) / 255; }
struct Model {
    bool visible = false, muted = false;
    uint8_t brightness = 255, row = 0;
    void load(bool mute, int level) {
        muted = mute;
        brightness = validBrightness(level) ? level : 255;
    }
    void open() {
        visible = true;
        row = 0;
    }
    void close() { visible = false; }
    void move(int direction) {
        if (visible)
            row = (int(row) + (direction < 0 ? kRows - 1 : 1)) % kRows;
    }
    bool activate() {
        if (!visible)
            return false;
        if (row == kSound) {
            muted = !muted;
            return true;
        }
        if (row == kBrightness) {
            brightness = brightness == 64    ? 128
                         : brightness == 128 ? 192
                         : brightness == 192 ? 255
                                             : 64;
            return true;
        }
        return false;
    }
};
struct HoldResult {
    bool open = false, consume = false;
};
class LeftHold {
  public:
    HoldResult update(uint32_t now, bool a, bool b, bool enabled) {
        if (drain_) {
            if (!a && !b)
                drain_ = false;
            return {false, true};
        }
        if (!a && !b) {
            armed_ = blocked_ = false;
            return {};
        }
        if (!enabled || b) {
            armed_ = false;
            blocked_ = true;
            return {};
        }
        if (!a || blocked_)
            return {};
        if (!armed_) {
            armed_ = true;
            at_ = now;
        }
        if (uint32_t(now - at_) < 600)
            return {};
        armed_ = false;
        drain_ = true;
        return {true, true};
    }

  private:
    bool armed_ = false, blocked_ = false, drain_ = false;
    uint32_t at_ = 0;
};
enum class Link { Offline, Usb, Wifi };
struct Snapshot {
    int battery = -1, charging = -1, rssi = 0;
    bool wifiSaved = false, wifiConnected = false;
    Link link = Link::Offline;
    bool voiceCapable = false, voiceReady = false, voiceBusy = false;
};
inline const char *agent(const Snapshot &s) {
    return s.link == Link::Usb ? "usb" : s.link == Link::Wifi ? "wifi" : "offline";
}
inline const char *voice(const Snapshot &s) {
    return s.link == Link::Offline ? "offline"
           : s.voiceBusy           ? "busy"
           : !s.voiceCapable       ? "unavailable"
           : s.voiceReady          ? "ready"
                                   : "offline";
}
inline const char *const labels[kRows] = {"Battery",     "Wi-Fi", "Agent",
                                          "Voice input", "Sound", "Brightness"};
inline void value(const Snapshot &s, const Model &m, int row, char *out, size_t size) {
    switch (row) {
    case 0:
        if (s.battery < 0 || s.battery > 100)
            snprintf(out, size, "Unavailable");
        else
            snprintf(out, size, "~%d%%%s", s.battery,
                     s.charging == 1   ? " charging"
                     : s.charging == 0 ? " battery"
                                       : "");
        break;
    case 1:
        if (s.wifiConnected)
            snprintf(out, size, "On %d dBm", s.rssi);
        else
            snprintf(out, size, "%s", s.wifiSaved ? "Connecting" : "Not set");
        break;
    case 2:
        snprintf(out, size, "%s",
                 s.link == Link::Usb    ? "USB"
                 : s.link == Link::Wifi ? "Wi-Fi"
                                        : "Offline");
        break;
    case 3: {
        const char *v = voice(s);
        snprintf(out, size, "%c%s", v[0] - 'a' + 'A', v + 1);
        break;
    }
    case 4:
        snprintf(out, size, "%s", m.muted ? "Muted" : "On");
        break;
    case 5:
        snprintf(out, size, "%d%%", percent(m.brightness));
        break;
    default:
        if (size)
            out[0] = 0;
    }
}
inline bool gear(double x, double y) { return x >= 208 && x <= 260 && y >= 30 && y <= 82; }
inline int hit(double x, double y) {
    if (x >= 158 && x <= 310 && y >= 392 && y <= 432)
        return -2;
    if (x < kLeft || x > kRight || y < kTop || y >= kTop + kPitch * kRows)
        return -1;
    const int offset = int(y - kTop);
    return offset % kPitch < kHeight ? offset / kPitch : -1;
}
} // namespace badgesettings
