#include "voice_button.h"
#include "buttons.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace badgevoice;
    HoldButton h;
    assert(h.update(0, false, true, true).edge == Edge::None);
    for (uint32_t t : {179U, 180U, 350U, 500U, 599U}) {
        const auto pending = h.update(t, false, true, true);
        assert(pending.edge == Edge::None && !pending.consume);
    }
    assert(h.update(600, false, true, true).edge == Edge::Start);
    assert(h.update(620, false, true, true).consume);
    assert(h.update(700, false, false, true).edge == Edge::Stop);
    assert(!h.update(730, false, false, true).consume);
    h.update(800, false, true, true);
    assert(h.update(1400, false, true, true).edge == Edge::Start);
    assert(h.update(1430, true, true, true).edge == Edge::Cancel);
    assert(h.update(1500, false, true, true).consume);
    assert(h.update(1600, false, false, true).consume);
    assert(!h.update(1601, false, false, true).consume);
    h.update(1700, false, true, true);
    assert(h.update(1780, true, true, true).edge == Edge::None);
    assert(h.update(2300, true, true, true).edge == Edge::None);
    h.update(2400, false, false, true);
    assert(h.update(2500, false, true, false).edge == Edge::None);
    assert(h.update(3500, false, true, false).edge == Edge::None);
    h.update(4000, false, true, true);
    assert(h.update(4600, false, true, true).edge == Edge::Start);
    assert(h.update(4620, false, true, false).edge == Edge::Cancel);
    h.update(4700, false, false, true);
    h.update(UINT32_MAX - 299, false, true, true);
    assert(h.update(299, false, true, true).edge == Edge::None);
    assert(h.update(300, false, true, true).edge == Edge::Start);

    // Slow page taps release once, preserve the reply, and never start listening.
    for (uint32_t duration : {100U, 180U, 250U, 400U, 500U, 599U}) {
        badgeinput::ButtonGrammar buttons;
        HoldButton tap;
        for (uint32_t now = 0; now < duration; ++now) {
            const auto gesture = tap.update(now, false, true, true);
            assert(gesture.edge == Edge::None && !gesture.consume);
            assert(buttons.update(now, false, true, false, false) == badgeinput::Input::None);
        }
        const auto released = tap.update(duration, false, false, true);
        assert(released.edge == Edge::None && !released.consume);
        assert(buttons.update(duration, false, false, false, true) == badgeinput::Input::Next);
        assert(buttons.update(duration + 1, false, false, false, false) == badgeinput::Input::None);
    }
    // Mirror the actual adapter: consumed samples reset page/chord grammar.
    badgeinput::ButtonGrammar buttons;
    HoldButton voice;
    int starts = 0, stops = 0, pages = 0;
    for (uint32_t now = 0; now <= 800; ++now) {
        const auto gesture = voice.update(now, false, now < 800, true);
        starts += gesture.edge == Edge::Start;
        stops += gesture.edge == Edge::Stop;
        if (gesture.consume)
            buttons = badgeinput::ButtonGrammar{};
        else
            pages +=
                buttons.update(now, false, now < 800, false, now == 800) == badgeinput::Input::Next;
    }
    assert(starts == 1 && stops == 1 && pages == 0);
    for (uint32_t overlap : {100U, 600U}) {
        HoldButton chordVoice;
        badgeinput::ButtonGrammar chord;
        for (uint32_t now = 0; now < overlap; ++now) {
            assert(chordVoice.update(now, true, true, true).edge == Edge::None);
            assert(chord.update(now, true, true, false, false) == badgeinput::Input::None);
        }
        assert(chordVoice.update(overlap, false, false, true).edge == Edge::None);
        assert(chord.update(overlap, false, false, true, true) ==
               (overlap == 600 ? badgeinput::Input::Escape : badgeinput::Input::Enter));
    }
    std::cout << "Voice button: 600 ms threshold, slow/repeated page taps, consumed voice release, "
                 "chord, cancellation and clock wrap passed\n";
}
