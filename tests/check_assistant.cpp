// Compile the actual firmware session handler with host I/O, clock and animation substitutes.
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "types.h"
using std::max;
static uint32_t clockMs = 100;
static uint32_t millis() { return clockMs; }
static struct {
    bool muted = false, visible = false;
} settings;
namespace badgevoice {
static bool recording = false;
static int cancellations = 0;
static void cancel() { ++cancellations; }
} // namespace badgevoice
namespace badgereply {
static struct {
    bool receiving = false;
} rx;
static int cancellations = 0;
static void cancel() { ++cancellations; }
} // namespace badgereply
static struct {
    std::string output;
    void println(const char *text) { output += std::string(text) + "\n"; }
    void printf(const char *format, ...) {
        char line[256];
        va_list args;
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        output += line;
    }
} netOut;
enum { SIdle, SThinking, SWorking, SBlocked, SWaiting, SSleeping, SSurprised, SDone };
enum { SndPop, SndChime, SndWince, SndStart };
static constexpr int kStyleCount = 8;
static int demoState = SIdle, demoStyle = 0, sound = -1, mood = SIdle, style = 0;
static void applyStyle(int value, bool) { style = value; }
static void enterState(int value, bool) { mood = value; }
static void playSound(int value) { sound = value; }
#include "assistant.h"
static void report(const char *value) {
    char line[200];
    snprintf(line, sizeof(line), "%s", value);
    handleAssistantLine(line);
}
int main() {
    assert(soundMuted());
    report("aaaa0001 thinking 0 Assistant|working");
    assert(assistant.used && !strcmp(assistant.id, "aaaa0001") && sound == SndPop);
    assert(assistantMood(assistant, clockMs) == SThinking && !soundMuted());
    assert(netOut.output == "GB_ACTIVE aaaa0001\n");
    report("bbbb0002 done 4 Other|must not replace");
    assert(!strcmp(assistant.name, "Assistant") && assistant.state == AssistantState::Thinking);
    assert(netOut.output.find("GB_ERROR assistant_busy") != std::string::npos);
    removeAssistant("bbbb0002");
    assert(assistant.used);
    clockMs = 500;
    report("aaaa0001 done 0 Assistant|your turn");
    assert(assistant.unseen && sound == SndChime);
    assert(assistantMood(assistant, 4500) == SWaiting);
    clockMs = 5000;
    report("aaaa0001 done 0 Assistant|your turn");
    assert(assistant.since == 500);
    assistant.unseen = false;
    assert(assistantMood(assistant, clockMs) == SIdle);
    report("aaaa0001 error 0 Assistant|failed");
    assert(assistantMood(assistant, clockMs) == SSurprised);
    assert(assistantMood(assistant, clockMs + 2500) == SBlocked);
    manualSleep = true;
    assert(soundMuted());
    const int cancelled = badgevoice::cancellations;
    removeAssistant("*");
    assert(!assistant.used && !manualSleep && mood == demoState && style == demoStyle);
    assert(badgevoice::cancellations == cancelled + 1);
    assert(badgereply::cancellations == badgevoice::cancellations);
    report("bbbb0002 idle 0 Replacement|ready");
    assert(!strcmp(assistant.id, "bbbb0002"));
    report("bad! idle 0 Invalid|ignored");
    assert(!strcmp(assistant.id, "bbbb0002"));
    assert(lastHubMs == 0 && lastInteractionMs == 0 && screensaverSince == 0 &&
           nextScreensaverStep == 0 && screensaverSteps == 0);
    puts(
        "Assistant: owner isolation, state age, acknowledgement, removal and cancellation passed.");
}
