#include "settings_core.h"
#include "buttons.h"
#include "voice_button.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

int main() {
    using namespace badgesettings;
    Model m;
    assert(!m.visible && !m.muted && m.brightness == 255);
    m.move(1);
    assert(m.row == 0 && !m.activate());
    m.open();
    m.move(-1);
    assert(m.row == 5);
    m.move(1);
    assert(m.row == 0);
    assert(!m.activate() && !m.muted && m.brightness == 255);
    m.row = kSound;
    assert(m.activate() && m.muted);
    assert(m.activate() && !m.muted);
    m.row = kBrightness;
    for (int expected : {64, 128, 192, 255}) {
        assert(m.activate());
        assert(m.brightness == expected);
    }
    m.row = kSound;
    m.activate();
    m.row = kBrightness;
    m.activate();
    m.close();
    m.open();
    assert(m.muted && m.brightness == 64 && m.row == 0);
    m.load(false, 17);
    assert(!m.muted && m.brightness == 255);
    for (int v = 0; v < 256; ++v) {
        m.load(true, v);
        assert(m.brightness == (validBrightness(v) ? v : 255));
    }
    assert(percent(64) == 25 && percent(128) == 50 && percent(192) == 75 && percent(255) == 100);
    LeftHold hold;
    assert(!hold.update(100, true, false, true).open);
    assert(!hold.update(699, true, false, true).consume);
    auto r = hold.update(700, true, false, true);
    assert(r.open && r.consume);
    assert(!hold.update(701, true, false, false).open);
    assert(hold.update(800, false, false, false).consume);
    assert(!hold.update(801, false, false, true).consume);
    hold = LeftHold{};
    hold.update(0, true, false, true);
    hold.update(80, true, true, true);
    hold.update(100, true, false, true);
    assert(!hold.update(700, true, false, true).open);
    hold.update(701, false, false, true);
    hold.update(800, true, false, true);
    assert(hold.update(1400, true, false, true).open);
    hold = LeftHold{};
    hold.update(0, true, false, false);
    assert(!hold.update(800, true, false, true).open);
    hold.update(801, false, false, true);
    hold.update(900, true, false, true);
    assert(hold.update(1500, true, false, true).open);
    hold = LeftHold{};
    hold.update(UINT32_MAX - 299, true, false, true);
    assert(hold.update(300, true, false, true).open);
    hold = LeftHold{};
    hold.update(0, true, false, true);
    assert(!hold.update(50, false, false, true).consume);
    badgeinput::ButtonGrammar chord;
    hold = LeftHold{};
    for (uint32_t t : {0U, 80U, 599U, 600U}) {
        assert(!hold.update(t, true, true, true).open);
        const auto input = chord.update(t, true, true, false, false);
        assert(input == (t == 600 ? badgeinput::Input::Escape : badgeinput::Input::None));
    }
    assert(chord.update(601, false, false, true, true) == badgeinput::Input::None);
    badgevoice::HoldButton voice;
    assert(voice.update(0, false, true, false).edge == badgevoice::Edge::None);
    assert(voice.update(600, false, true, false).edge == badgevoice::Edge::None);
    voice.update(601, false, false, false);
    voice.update(700, false, true, true);
    assert(voice.update(1300, false, true, true).edge == badgevoice::Edge::Start);
    Snapshot s;
    char out[32];
    auto text = [&](int row) {
        value(s, m, row, out, sizeof(out));
        return out;
    };
    assert(!strcmp(text(0), "Unavailable"));
    s.battery = 101;
    assert(!strcmp(text(0), "Unavailable"));
    s.battery = 87;
    s.charging = 1;
    assert(!strcmp(text(0), "~87% charging"));
    s.charging = 0;
    assert(!strcmp(text(0), "~87% battery"));
    assert(!strcmp(text(1), "Not set"));
    s.wifiSaved = true;
    assert(!strcmp(text(1), "Connecting"));
    s.wifiConnected = true;
    s.rssi = -43;
    assert(!strcmp(text(1), "On -43 dBm"));
    assert(!strcmp(text(2), "Offline"));
    assert(!strcmp(text(3), "Offline"));
    s.link = Link::Usb;
    assert(!strcmp(text(2), "USB"));
    assert(!strcmp(text(3), "Unavailable"));
    s.voiceCapable = s.voiceReady = true;
    assert(!strcmp(text(3), "Ready"));
    s.voiceBusy = true;
    assert(!strcmp(text(3), "Busy"));
    s.link = Link::Wifi;
    assert(!strcmp(text(2), "Wi-Fi"));
    m.muted = true;
    assert(!strcmp(text(4), "Muted"));
    char tiny[2] = {'x', 'x'};
    value(s, m, 0, tiny, sizeof(tiny));
    assert(tiny[1] == 0);
    assert(gear(234, 56) && !gear(234, 200) && !gear(NAN, 56));
    for (int row = 0; row < kRows; ++row) {
        const int y = kTop + row * kPitch;
        assert(hit(234, y + 17) == row && hit(234, y + 35) == -1);
        for (int x : {kLeft, kRight})
            for (int yy : {y, y + kHeight})
                assert(std::hypot(x - 234, yy - 233) < 226);
    }
    assert(hit(234, 412) == -2 && hit(10, 233) == -1 && hit(234, 40) == -1);
    std::cout << "Settings: defaults, bounds, persistence model, status truth, left hold, "
                 "chord/voice isolation and round-safe hit regions passed\n";
}
