// Real pinned M5Unified debounce/click classification feeding the shared gesture cores.
#include "Button_Class.hpp"
#include "voice_button.h"
#include "buttons.h"
#include <cassert>
#include <iostream>

struct Harness {
    m5::Button_Class a, b;
    badgevoice::HoldButton hold;
    badgeinput::ButtonGrammar pages;
    bool replyVisible = true;
    int starts = 0, stops = 0, cancels = 0, next = 0, enter = 0, escape = 0;
    Harness() { b.setHoldThresh(badgevoice::kHoldMs); }
    void sample(uint32_t now, bool rawA, bool rawB) {
        a.setRawState(now, rawA);
        b.setRawState(now, rawB);
        const auto g = hold.update(now, a.isPressed(), b.isPressed(), true);
        if (g.edge == badgevoice::Edge::Start) {
            ++starts;
            replyVisible = false;
        }
        if (g.edge == badgevoice::Edge::Stop)
            ++stops;
        if (g.edge == badgevoice::Edge::Cancel)
            ++cancels;
        if (g.consume) {
            pages = badgeinput::ButtonGrammar{};
            return;
        }
        const auto input =
            pages.update(now, a.isPressed(), b.isPressed(), a.wasClicked(), b.wasClicked());
        next += input == badgeinput::Input::Next;
        enter += input == badgeinput::Input::Enter;
        escape += input == badgeinput::Input::Escape;
    }
};
int main() {
    for (uint32_t duration : {100U, 180U, 250U, 400U, 500U, 550U, 599U}) {
        Harness h;
        for (uint32_t now = 0; now <= duration + 30; ++now)
            h.sample(now, false, now < duration);
        assert(h.starts == 0 && h.stops == 0 && h.cancels == 0 && h.next == 1 && h.replyVisible);
    }
    Harness threshold;
    for (uint32_t now = 0; now <= 609; ++now)
        threshold.sample(now, false, true);
    assert(threshold.starts == 0 && threshold.replyVisible);
    threshold.sample(610, false, true);
    assert(threshold.starts == 1 && !threshold.replyVisible);
    for (uint32_t now = 611; now <= 750; ++now)
        threshold.sample(now, false, now < 700);
    assert(threshold.starts == 1 && threshold.stops == 1 && threshold.next == 0);
    Harness repeated;
    uint32_t at = 0;
    for (uint32_t duration : {250U, 550U, 180U, 599U, 400U}) {
        for (uint32_t now = at; now <= at + duration + 50; ++now)
            repeated.sample(now, false, now < at + duration);
        at += duration + 51;
    }
    assert(repeated.next == 5 && repeated.starts == 0 && repeated.replyVisible);
    Harness chord;
    for (uint32_t now = 0; now <= 800; ++now)
        chord.sample(now, now < 700, now < 700);
    assert(chord.starts == 0 && chord.next == 0 && chord.escape == 1 && chord.enter == 0);
    Harness shortChord;
    for (uint32_t now = 0; now <= 200; ++now)
        shortChord.sample(now, now < 150, now < 150);
    assert(shortChord.starts == 0 && shortChord.next == 0 && shortChord.enter == 1);
    std::cout << "Pinned M5Unified buttons: 180-599 ms and repeated presses page with reply "
                 "retained; 600 ms listening, release consumption and chords passed\n";
}
