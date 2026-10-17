#pragma once

static constexpr int kDemoCycle[] = {SIdle,    SHappy,   SCurious, SExcited, SSurprised, SThinking,
                                     SWorking, SWaiting, SBlocked, SDone,    SSad};
static constexpr int kDemoCycleLen = sizeof(kDemoCycle) / sizeof(kDemoCycle[0]);

static void handleInput(badgeinput::Input in) {
    if (in != badgeinput::Input::None)
        lastInteractionMs = millis();
    if (settings.visible) {
        if (in == badgeinput::Input::Prev)
            settings.move(-1);
        else if (in == badgeinput::Input::Next)
            settings.move(1);
        else if (in == badgeinput::Input::Enter)
            settingsActivate();
        else if (in == badgeinput::Input::Escape)
            settingsClose();
        return;
    }
    if (badgereply::visible && in != badgeinput::Input::None) {
        if (in == badgeinput::Input::Prev)
            replyPage(-1);
        else if (in == badgeinput::Input::Next)
            replyPage(1);
        else {
            badgereply::cancel();
            fullRedraw = true;
            if (in == badgeinput::Input::Escape)
                manualSleep = !manualSleep;
        }
        return;
    }
    if (assistant.used) { // acknowledge or wake the Assistant
        switch (in) {
        case badgeinput::Input::Prev:
        case badgeinput::Input::Next:
            wake();
            assistant.unseen = false;
            netOut.printf("GB_ACTIVE %s\n", assistant.id);
            break;
        case badgeinput::Input::Enter:
            wake();
            assistant.unseen = false;
            startBounce();
            netOut.printf("GB_ACK %s\n", assistant.id);
            break;
        case badgeinput::Input::Escape:
            manualSleep = !manualSleep;
            break;
        default:
            break;
        }
        return;
    }
    switch (in) { // demo mode
    case badgeinput::Input::Prev:
        wake();
        demoStyle = (demoStyle + kStyleCount - 1) % kStyleCount;
        applyStyle(demoStyle, true);
        break;
    case badgeinput::Input::Next:
        wake();
        demoStyle = (demoStyle + 1) % kStyleCount;
        applyStyle(demoStyle, true);
        break;
    case badgeinput::Input::Enter: {
        int k = 0;
        while (k < kDemoCycleLen && kDemoCycle[k] != state)
            ++k;
        demoState =
            state == SSleeping || k >= kDemoCycleLen ? SIdle : kDemoCycle[(k + 1) % kDemoCycleLen];
        enterState(demoState, true);
        // Audition each state's transition sound.
        if (demoState == SDone)
            playSound(SndChime);
        else if (demoState == SBlocked)
            playSound(SndUhoh);
        else if (demoState == SSurprised)
            playSound(SndWince);
        else if (demoState == SThinking || demoState == SWorking)
            playSound(SndStart);
        break;
    }
    case badgeinput::Input::Escape:
        demoState = state == SSleeping ? SIdle : SSleeping;
        enterState(demoState, true);
        break;
    default:
        break;
    }
}

static void handleButtons(uint32_t now) {
    const auto menu =
        settingsHold.update(now, M5.BtnA.isPressed(), M5.BtnB.isPressed(),
                            !settings.visible && !badgereply::visible && !calibration.active() &&
                                !badgevoice::recording && !badgevoice::sending);
    if (menu.open)
        settingsOpen();
    if (menu.consume) {
        buttons = badgeinput::ButtonGrammar{};
        return;
    }
    const auto gesture =
        badgevoice::hold.update(now, M5.BtnA.isPressed(), M5.BtnB.isPressed(),
                                !settings.visible && !calibration.active() && assistant.used &&
                                    assistant.voice && badgevoice::ready());
    if (gesture.edge == badgevoice::Edge::Start) {
        wake();
        fullRedraw = true;
        badgevoice::start(assistant.id);
    } else if (gesture.edge == badgevoice::Edge::Stop)
        badgevoice::stop();
    else if (gesture.edge == badgevoice::Edge::Cancel)
        badgevoice::cancel();
    if (gesture.consume) {
        buttons = badgeinput::ButtonGrammar{};
        return;
    }
    const auto input = buttons.update(now, M5.BtnA.isPressed(), M5.BtnB.isPressed(),
                                      M5.BtnA.wasClicked(), M5.BtnB.wasClicked());
    if (calibration.active())
        calibrationInput(input);
    else
        handleInput(input);
}

static bool onBody(float x, float y) {
    const float u = (x - (kHomeX + dragX.x)) / kScale, v = (y - (kHomeY + dragY.x)) / kScale;
    return sampleSdf(sdfCur, u + C, v + C) < 14;
}

static void handleTouch(uint32_t now) {
    if (!touchMapVersion)
        return;
    lgfx::touch_point_t raw;
    const bool pressed = M5.Display.getTouchRaw(&raw) > 0;
    if (pressed) {
        auto p = touchcal::applyWarp(touchMap, {double(raw.x), double(raw.y)});
        touchX = clampf(p.x, 0, W - 1);
        touchY = clampf(p.y, 0, H - 1);
        lastTouchMs = now;
        lastInteractionMs = now;
        if (!touchDown) {
            touchDown = true;
            touchStartMs = now;
            touchStartX = touchX;
            touchStartY = touchY;
            wake();
            dragging = false;
            grabDX = touchX - (kHomeX + dragX.x);
            grabDY = touchY - (kHomeY + dragY.x);
        } else if (!dragging && onBody(touchStartX, touchStartY) &&
                   hypotf(touchX - touchStartX, touchY - touchStartY) > 14) {
            dragging = true;
            netOut.println("GB_EVENT drag");
        }
    } else if (touchDown) {
        touchDown = false;
        const bool tap =
            now - touchStartMs < 350 && hypotf(touchX - touchStartX, touchY - touchStartY) < 14;
        if (dragging) {
            sqy.v -= 4;
            sqx.v += 3.5f;
            dragging = false;
        } else if (tap && badgesettings::gear(touchStartX, touchStartY) &&
                   badgesettings::gear(touchX, touchY))
            settingsOpen();
        else if (tap && onBody(touchX, touchY)) {
            startBounce();
            if (frand(0, 1) < .3f)
                startSpin();
            netOut.println("GB_EVENT bounce");
        } else if (tap) {
            const float dx = touchX - kHomeX, dy = touchY - kHomeY, len = fmaxf(hypotf(dx, dy), 1);
            gazeX.t = dx / len * 15;
            gazeY.t = dy / len * 9;
            gazeHoldUntil = now + 1800;
            netOut.printf("GB_EVENT look x=%d y=%d\n", int(touchX), int(touchY));
        }
    }
}

// Tilt: the assistant sloshes when the board's orientation changes, then drifts back to centre, so any
// resting pose (flat, upright on its lanyard, leaning) keeps it centred.
// Axis mapping measured on the device, lanyard down and standing upright: ax = -0.83, so IMU +x
// points toward the lanyard (screen down) and, right-handed with +z out of the screen, IMU +y
// points screen-left. "Downhill" in screen space is (ay, -ax).
static void handleImu(uint32_t now) {
    if (!imuOk || !M5.Imu.update())
        return;
    static uint32_t lastMs = 0;
    static float baseX = 0, baseY = 0;
    static bool haveBase = false;
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    const float downX = ay, downY = -ax; // downhill direction on the screen (in g)
    const float dt = lastMs ? clampf((now - lastMs) / 1000.0f, 0, .2f) : 0;
    lastMs = now;
    if (!haveBase) {
        baseX = downX;
        baseY = downY;
        haveBase = true;
    }
    const float k = 1 - expf(-dt / 2.5f); // baseline follows the resting pose (tau 2.5 s)
    baseX += (downX - baseX) * k;
    baseY += (downY - baseY) * k;
    float tx = downX - baseX, ty = downY - baseY;
    if (fabsf(tx) < .05f)
        tx = 0; // deadband: sensor noise never nudges the assistant
    if (fabsf(ty) < .05f)
        ty = 0;
    tiltX = mixf(tiltX, clampf(tx * 1.5f, -1, 1), .2f);
    tiltY = mixf(tiltY, clampf(ty * 1.5f, -1, 1), .2f);
    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (mag > 2.2f && state != SSleeping) {
        startBounce();
        startSpin();
    }
    if (now - lastImuLog > 5000) {
        lastImuLog = now;
        netOut.printf("GB_IMU ax=%.2f ay=%.2f az=%.2f tilt=%.2f,%.2f\n", ax, ay, az, tiltX, tiltY);
    }
}
