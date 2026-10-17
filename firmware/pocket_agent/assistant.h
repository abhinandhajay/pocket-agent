#pragma once

// One host-owned Assistant.
static const char *const kAssistantStateNames[] = {"idle", "thinking", "tool",    "error",
                                                   "done", "waiting",  "sleeping"};
static Assistant assistant; // unused = demo mode (no hub data)
static bool manualSleep = false;
// Silent in manual sleep and whenever there are no sessions (the demo is the screensaver).
static bool soundMuted() {
    return settings.muted || settings.visible || manualSleep || !assistant.used ||
           badgevoice::recording || badgereply::rx.receiving;
}
static uint32_t lastHubMs = 0;         // last line received from the hub (USB or Wi-Fi)
static uint32_t lastInteractionMs = 0; // last button or touch
static uint32_t screensaverSince = 0, nextScreensaverStep = 0;
static int screensaverSteps = 0;

static bool needsYou(AssistantState s) {
    return s == AssistantState::Done || s == AssistantState::Error || s == AssistantState::Waiting;
}
static bool busy(AssistantState s) {
    return s == AssistantState::Thinking || s == AssistantState::Tool;
}

static int assistantMood(const Assistant &p, uint32_t now) {
    const uint32_t age =
        uint32_t(max<int32_t>(0, int32_t(now - p.since))); // since may postdate this frame
    if (p.errorAt && int32_t(now - p.errorAt) < 2500 && p.state != AssistantState::Done)
        return SSurprised;
    switch (p.state) {
    case AssistantState::Thinking:
        return SThinking;
    case AssistantState::Tool:
        return SWorking;
    case AssistantState::Error:
        return SBlocked;
    case AssistantState::Waiting:
        return SWaiting;
    case AssistantState::Sleeping:
        return SSleeping;
    case AssistantState::Done:
        if (age < 4000)
            return SDone;
        if (p.unseen && age < 30000)
            return SWaiting;
        break;
    case AssistantState::Idle:
        break;
    }
    return age > 10 * 60000UL ? SSleeping : SIdle;
}

// "assistant <id> <state> <style> <name>|<detail>"
static void handleAssistantLine(char *args) {
    char *id = strtok(args, " "), *st = strtok(nullptr, " "), *sty = strtok(nullptr, " "),
         *rest = strtok(nullptr, "");
    if (!id || !st || !sty) {
        netOut.println("GB_ERROR bad_assistant");
        return;
    }
    bool valid = strlen(id) <= 8 && strlen(id) > 0;
    for (const char *c = id; *c; ++c)
        valid = valid && isalnum(static_cast<unsigned char>(*c));
    int stateIndex = -1;
    for (int k = 0; k < 7; ++k)
        if (!strcmp(st, kAssistantStateNames[k]))
            stateIndex = k;
    char *end = nullptr;
    const long styleValue = strtol(sty, &end, 10);
    if (!valid || stateIndex < 0 || !end || *end || styleValue < 0 || styleValue >= kStyleCount ||
        !rest || !strchr(rest, '|')) {
        netOut.println("GB_ERROR bad_assistant");
        return;
    }
    const AssistantState ps = AssistantState(stateIndex);
    char *bar = strchr(rest, '|');
    *bar = 0;
    const char *name = rest, *detail = bar + 1;
    const uint32_t now = millis();
    if (assistant.used && strcmp(assistant.id, id)) {
        netOut.println("GB_ERROR assistant_busy");
        return;
    }
    const bool fresh = !assistant.used;
    if (fresh) {
        badgevoice::cancel();
        badgereply::cancel();
        assistant = Assistant{};
        assistant.used = true;
        strncpy(assistant.id, id, sizeof(assistant.id) - 1);
        assistant.state = ps;
        assistant.since = now;
        netOut.printf("GB_ACTIVE %s\n", assistant.id);
    }
    Assistant &p = assistant;
    const uint8_t style = uint8_t(styleValue);
    const bool restyled = p.style != style;
    p.style = style;
    strncpy(p.name, name, sizeof(p.name) - 1);
    strncpy(p.detail, detail, sizeof(p.detail) - 1);
    p.detail[sizeof(p.detail) - 1] = 0;
    if (ps == AssistantState::Error) {
        p.errorAt = now ? now : 1;
        p.uhohDone = false;
    }
    // Transition sounds: a new session pops; a run starting ticks (not thinking <-> tool flips);
    // completion chimes; an error winces (tick() adds the uh-oh if it persists).
    const AssistantState prevState = p.state;
    auto resting = [](AssistantState s2) {
        return s2 == AssistantState::Idle || s2 == AssistantState::Done ||
               s2 == AssistantState::Waiting || s2 == AssistantState::Sleeping;
    };
    int sound = -1; // played muting depends on the resulting Assistant state
    if (fresh)
        sound = SndPop;
    else if (prevState != ps && ps == AssistantState::Done)
        sound = SndChime;
    else if (prevState != ps && ps == AssistantState::Error)
        sound = SndWince;
    else if (prevState != ps && busy(ps) && resting(prevState))
        sound = SndStart;
    if (p.state != ps || fresh) {
        p.state = ps;
        p.since = now;
        if (needsYou(ps))
            p.unseen = true;
    }
    if (fresh || restyled)
        applyStyle(style, false);
    if (sound >= 0)
        playSound(sound);
}

static void removeAssistant(const char *id) {
    if (!assistant.used || (strcmp(id, "*") && strcmp(assistant.id, id)))
        return;
    badgevoice::cancel();
    badgereply::cancel();
    assistant = Assistant{};
    applyStyle(demoStyle, true);
    manualSleep = false;
    enterState(demoState, false);
}
