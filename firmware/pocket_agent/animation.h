#pragma once

// Original motion targets for the same Pocket robot in all twelve moods.
static void motion(uint32_t now, float &lidT, float &eyeT) {
    const float t = now / 1000.0f, age = (now - stateSince) / 1000.0f;
    d.t = 1.2f * sinf(t * .8f);
    o.t = 0;
    A.t = 2 * sinf(t * 1.6f);
    h.t = 1;
    lidT = 1;
    eyeT = 1;
    switch (state) {
    case SThinking:
        d.t = -7 + 2 * sinf(t * 1.1f);
        A.t = -2 + sinf(t * 1.7f);
        break;
    case SWorking:
        d.t = 2 * sinf(t * 5);
        A.t = 2 * sinf(t * 6);
        h.t = 1 + .012f * sinf(t * 6);
        break;
    case SWaiting:
        d.t = 3;
        A.t = 4 + sinf(t);
        break;
    case SBlocked:
        d.t = 3 * sinf(age * 18) * expf(-fmodf(age, 3) * 5);
        A.t = 0;
        break;
    case SDone:
        d.t = 4 * sinf(t * 3);
        eyeT = 1.04f;
        break;
    case SSurprised:
        A.t = -9 * expf(-age * 3);
        h.t = 1 + .04f * expf(-age * 4);
        eyeT = 1.08f;
        break;
    case SSleeping:
        d.t = 6;
        A.t = 8 + 2 * sinf(t * 1.2f);
        break;
    case SHappy:
        d.t = 5 * sinf(t * 2.2f);
        A.t = -3 * fabsf(sinf(t * 2.2f));
        break;
    case SCurious:
        d.t = 11 * sinf(t * .9f);
        o.t = 3 * sinf(t * .9f);
        eyeT = 1.03f;
        break;
    case SExcited:
        d.t = 5 * sinf(t * 6);
        A.t = -7 * fabsf(sinf(t * 5));
        h.t = 1 + .025f * sinf(t * 5);
        break;
    case SSad:
        d.t = -5;
        A.t = 7 + sinf(t * 1.2f);
        eyeT = .94f;
        break;
    default:
        break;
    }
    if ((state == SHappy || state == SExcited) && int32_t(now - nextSpecial) >= 0) {
        startBounce();
        nextSpecial = now + urand(4000, 9000);
    }
}

// Screensaver cycles moods on the same robot. Interaction pauses it for a minute.
static constexpr int kScreensaverStates[] = {SIdle,    SHappy,   SCurious, SThinking, SWorking,
                                             SExcited, SWaiting, SDone,    SSurprised};
static void updateScreensaver(uint32_t now) {
    if (assistant.used || manualSleep) {
        screensaverSince = 0;
        return;
    }
    if (!screensaverSince) {
        screensaverSince = now;
        nextScreensaverStep = now + 12000;
    }
    if (int32_t(now - lastInteractionMs) < 60000 && lastInteractionMs) {
        nextScreensaverStep = now + 12000;
        return;
    }
    if (int32_t(now - nextScreensaverStep) < 0)
        return;
    nextScreensaverStep = now + 12000;
    constexpr int n = sizeof(kScreensaverStates) / sizeof(kScreensaverStates[0]);
    int k = 0;
    while (k < n && kScreensaverStates[k] != state)
        ++k;
    demoState = kScreensaverStates[(k + 1) % n];
    enterState(demoState, false);
    ++screensaverSteps;
}

// Brightness: full after any interaction; the screensaver dims after a minute and more after ten;
// a quiet Assistant state (no assistant working or needing you for 5 min) dims; sleeping is dimmest.
static void updateBrightness(uint32_t now) {
    static int current = -1;
    static uint32_t quietSince = 0;
    const uint32_t sinceTouch = lastInteractionMs ? now - lastInteractionMs : now;
    int target = 255;
    if (state == SSleeping)
        target = 70;
    else if (!assistant.used) {
        const uint32_t sinceSaver = screensaverSince ? now - screensaverSince : 0;
        const uint32_t quiet = min(sinceTouch, sinceSaver);
        target = quiet < 60000 ? 255 : quiet < 600000 ? 120 : 50;
    } else {
        const bool busyOrNeeds =
            busy(assistant.state) || needsYou(assistant.state) || assistant.unseen;
        if (busyOrNeeds || !quietSince)
            quietSince = busyOrNeeds ? 0 : now;
        target = (!busyOrNeeds && quietSince && now - quietSince > 300000 && sinceTouch > 300000)
                     ? 120
                     : 255;
    }
    if (sinceTouch < 60000 && state != SSleeping)
        target = 255;
    if (settings.visible)
        target = 255;
    target = min(target, int(settings.brightness));
    if (target != current) {
        M5.Display.setBrightness(target);
        current = target;
    }
}

static void tick(float dt, uint32_t now) {
    // The hub pings every 5 s; 20 s of silence means it quit or the link is gone: clear the stale
    // Assistant state (the hub resyncs everything when it reconnects) and fall back to the screensaver.
    if (int(assistant.used) && int32_t(now - lastHubMs) > 20000) {
        removeAssistant("*");
        netOut.println("GB_EVENT hub_gone");
    }
    updateScreensaver(now);
    updateBrightness(now);
    {
        const float target = assistant.used ? kHomeYBand : kHomeYCentred; // ease ~0.3 s
        kHomeY += (target - kHomeY) * (1 - expf(-dt / .12f));
    }
    { // still errored when the 2.5 s wince ends: blocked -> "uh-oh"
        Assistant &p = assistant;
        // Signed difference: serial input can stamp errorAt after this frame's `now`.
        if (p.used && p.state == AssistantState::Error && p.errorAt && !p.uhohDone &&
            int32_t(now - p.errorAt) >= 2500) {
            p.uhohDone = true;
            playSound(SndUhoh);
        }
    }
    if (assistant.used) {
        const int want = manualSleep ? SSleeping : assistantMood(assistant, now);
        if (want != state)
            enterState(want, false);
        badge = assistant.unseen;
    } else {
        badge = false;
    }
    const StateDef &def = kStates[state];

    // Expressions, blinks, gaze schedule.
    if (now >= nextExpr) {
        const int next = def.expr[def.nExpr > 1 ? (esp_random() % def.nExpr) : 0];
        changeExpr(next, def.exprSpeed, now);
        nextExpr = now + urand(def.exprMin, def.exprMax);
    }
    if (pendingExpr >= 0 && now >= pendingAt) {
        setExpr(pendingExpr, pendingSpeed);
        pendingExpr = -1;
    }
    if (def.blinkMax && now >= nextBlink) {
        blinkNow(now);
        nextBlink = now + urand(def.blinkMin, def.blinkMax);
    }
    if (def.winks && now >= nextWink) {
        winkAt = now;
        winkEye = esp_random() & 1;
        nextWink = now + urand(4500, 10000);
    }
    if (now >= nextGaze && now >= gazeHoldUntil) {
        const float sx =
            (def.gxSigned ? (frand(0, 1) < .5f ? -1 : 1) : 1) * frand(def.gx0, def.gx1) * 15;
        gazeX.t = sx;
        gazeY.t = frand(def.gy0, def.gy1) * 9;
        nextGaze = now + urand(def.gazeMin, def.gazeMax);
    }
    if (touchDown && !dragging) { // look at the finger
        const float dx = touchX - (kHomeX + dragX.x), dy = touchY - (kHomeY + dragY.x);
        const float len = hypotf(dx, dy), m = clampf(len / 150, 0, 1) / fmaxf(len, 1);
        gazeX.t = dx * m * 15;
        gazeY.t = dy * m * 9;
    }

    float lidT, eyeT;
    motion(now, lidT, eyeT);
    eyeScale.t = eyeT;
    // Lid follows blink keyframes while any remain, otherwise the state's openness.
    float key = -1;
    while (lidKeys && now >= lidKeyAt[0]) {
        key = lidKeyV[0];
        for (int i = 1; i < lidKeys; ++i) {
            lidKeyAt[i - 1] = lidKeyAt[i];
            lidKeyV[i - 1] = lidKeyV[i];
        }
        --lidKeys;
    }
    if (key >= 0)
        lid.t = key;
    else if (!lidKeys)
        lid.t = lidT;

    // Drag / tilt.
    if (dragging) {
        dragX.t = clampf(touchX - grabDX - kHomeX, -140, 140);
        dragY.t = clampf(touchY - grabDY - kHomeY, -120, 110);
    } else {
        dragX.t = clampf(tiltX, -1, 1) * 40;
        dragY.t = clampf(tiltY, -1, 1) * 30;
    }
    sqx.t = 1;
    sqy.t = 1;

    // Small fixed substeps keep the robot stable when frame timing varies.
    const int n = max(1, int(ceilf(dt * 120)));
    const float step = dt / n;
    for (int i = 0; i < n; ++i) {
        blend.step(exprSpeed, 1, step);
        d.step(8, 1, step);
        o.step(8, 1, step);
        A.step(9, .9f, step);
        h.step(12, 1, step);
        lid.step(32, 1, step);
        eyeScale.step(12, 1, step);
        gazeX.step(11, 1, step);
        gazeY.step(11, 1, step);
        if (spinning)
            spin.step(8, 1, step);
        inkR.step(9, 1, step);
        inkG.step(9, 1, step);
        inkB.step(9, 1, step);
        sqx.step(16, .35f, step);
        sqy.step(16, .35f, step);
        dragX.step(dragging ? 18 : 7, dragging ? 1 : .6f, step);
        dragY.step(dragging ? 18 : 7, dragging ? 1 : .6f, step);
    }
    if (spinning && fabsf(spin.t - spin.x) < .004f && fabsf(spin.v) < .015f) {
        spinning = false;
        spin.init(0);
    }

    for (int i = 0; i < partCount;) { // confetti
        Particle &p = parts[i];
        p.vy += 900 * dt;
        p.vx *= 1 - 1.2f * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rot += p.vr * dt;
        p.life += dt;
        if (p.life > 1.8f || p.y > H + 20)
            parts[i] = parts[--partCount];
        else
            ++i;
    }
}
