#pragma once

// Frame recorder (evidence): N frames of a cropped, downsampled region at the loop's own rate.
static uint16_t *recBuf = nullptr;
static size_t recCapacity = 0;
static int recN = 0, recI = 0, recX = 0, recY = 0, recSize = 0, recStep = 2, recOut = 0;
static uint32_t *recT = nullptr;
static void recordFrame(uint32_t now) {
    if (!recBuf || recI >= recN)
        return;
    uint16_t *dst = recBuf + size_t(recI) * recOut * recOut;
    for (int y = 0; y < recOut; ++y) {
        const uint16_t *row = fb + (recY + y * recStep) * W + recX;
        for (int x = 0; x < recOut; ++x)
            dst[y * recOut + x] = row[x * recStep];
    }
    recT[recI++] = now;
    if (recI == recN)
        Serial.printf("GB_REC_DONE n=%d\n", recN);
}
// A requested dump may wait for its reader, but must honor short writes and yield.
// Small paced writes avoid CDC's disconnect/backpressure FIFO replacement policy.
static bool writeBulk(const uint8_t *data, size_t size) {
    size_t sent = 0;
    uint32_t lastProgress = millis();
    Serial.setTxTimeoutMs(25);
    while (sent < size) {
        const size_t written = Serial.write(data + sent, min<size_t>(256, size - sent));
        if (written) {
            sent += written;
            lastProgress = millis();
        }
        if (millis() - lastProgress > 3000) {
            Serial.setTxTimeoutMs(0);
            return false;
        }
        delay(1);
    }
    Serial.setTxTimeoutMs(2000);
    Serial.flush();
    return true;
}

static void dumpRec() {
    Serial.setTxTimeoutMs(2000); // a reader asked for this dump: let bulk writes wait for it
    Serial.printf("GB_REC %d %d\n", recI, recOut);
    for (int i = 0; i < recI; ++i)
        Serial.printf("%u%c", unsigned(recT[i]), i + 1 < recI ? ' ' : '\n');
    Serial.flush();
    const uint8_t *p = reinterpret_cast<const uint8_t *>(recBuf);
    const size_t total = size_t(recI) * recOut * recOut * 2;
    if (!writeBulk(p, total))
        return;
    Serial.println("\nGB_REC_END");
    Serial.setTxTimeoutMs(0);
}

static void dumpFrame() {
    Serial.setTxTimeoutMs(2000); // a reader asked for this dump: let bulk writes wait for it
    Serial.printf("GB_SHOT %d %d\n", W, H);
    Serial.flush();
    const uint8_t *p = reinterpret_cast<const uint8_t *>(fb);
    const size_t total = size_t(W) * H * 2;
    if (!writeBulk(p, total))
        return;
    Serial.println("\nGB_SHOT_END");
    Serial.setTxTimeoutMs(0);
}

static int stateByName(const char *name) {
    for (int i = 0; i < kStateCount; ++i)
        if (!strcmp(kStateNames[i], name))
            return i;
    return -1;
}

static void handleSerial() {
    static char line[768];
    static size_t n = 0;
    static bool overflow = false;
    size_t consumed = 0, lines = 0;
    while (Serial.available() && consumed++ < 8192 && lines < 8) {
        char c = Serial.read();
        if (c != '\n' && c != '\r') {
            if (n < sizeof(line) - 1)
                line[n++] = c;
            else
                overflow = true;
            continue;
        }
        line[n] = 0;
        n = 0;
        lines++;
        if (overflow) {
            overflow = false;
            netOut.println("GB_ERROR line_too_long");
            continue;
        }
        if (strlen(line) > 199 && strncmp(line, "reply data ", 11)) {
            netOut.println("GB_ERROR line_too_long");
            continue;
        }
        if (line[0])
            processLine(line, true);
    }
}

// One protocol line from USB (fromUsb) or from the authenticated Wi-Fi link.
static void processLine(char *line, bool fromUsb) {
    {
        lastHubMs = millis();
        if (badgereply::process(line, assistant.used ? assistant.id : "",
                                !settings.visible && !manualSleep && !calibration.active()))
            return;
        if (!strcmp(line, "settings")) {
            settingsInfo();
            return;
        }
        if (!strcmp(line, "settings open")) {
            if (settingsOpen())
                settingsInfo();
            return;
        }
        if (!strcmp(line, "settings close")) {
            settingsClose();
            settingsInfo();
            return;
        }
        if (!strcmp(line, "ping"))
            return; // hub heartbeat
        if (!strcmp(line, "voice")) {
            netOut.printf("GB_VOICE_STATUS recording=%d sending=%d samples=%u mic=%d link=%s "
                          "revision=%llu begin=%d challenge=%d hold_ms=%u click_hold_ms=%u\n",
                          badgevoice::recording, badgevoice::sending, unsigned(badgevoice::samples),
                          M5.Mic.isEnabled(), badgevoice::channel,
                          static_cast<unsigned long long>(badgevoice::selection.revision()),
                          badgevoice::beginSent, badgevoice::challenge[0] != 0,
                          unsigned(badgevoice::kHoldMs), unsigned(M5.BtnB.getHoldThresh()));
            return;
        }
        if (!strncmp(line, "voice ", 6)) {
            char target[9], take[17], value[16], nonce[33];
            int flag;
            if (sscanf(line, "voice target %8s %d", target, &flag) == 2 &&
                (flag == 0 || flag == 1)) {
                if (assistant.used && !strcmp(assistant.id, target)) {
                    assistant.voice = flag;
                    if (!flag && !strcmp(target, badgevoice::id) &&
                        (badgevoice::recording || badgevoice::sending))
                        badgevoice::cancel();
                }
            } else if (sscanf(line, "voice challenge %16s %32s", take, nonce) == 2)
                badgevoice::acceptChallenge(take, nonce);
            else if (sscanf(line, "voice feedback %16s %15s", take, value) == 2)
                badgevoice::feedback(take, value);
            else if (!strncmp(line, "voice link ", 11)) {
                char kind[5], revision[17], extra[2];
                const int fields = sscanf(line + 11, "%4s %16s %1s", kind, revision, extra);
                if (fields == 2)
                    badgevoice::link(kind, revision);
                else
                    netOut.println("GB_ERROR bad_voice");
            } else if (!strncmp(line, "voice test start", 16)) {
                char intended[9], extra[2];
                if (sscanf(line, "voice test start %8s %1s", intended, extra) == 1 &&
                    !settings.visible && assistant.used && assistant.voice &&
                    !strcmp(intended, assistant.id))
                    badgevoice::start(assistant.id);
                else
                    netOut.println("GB_ERROR voice_test_target");
            } else if (!strcmp(line, "voice test stop"))
                badgevoice::stop();
            else if (!strcmp(line, "voice test cancel"))
                badgevoice::cancel();
            else
                netOut.println("GB_ERROR bad_voice");
            return;
        }
        if (!strcmp(line, "hello")) {
            netlink::hello(netOut);
            return;
        }
        if (!strncmp(line, "wifi ", 5) || !strncmp(line, "pair ", 5) || !strcmp(line, "forget")) {
            if (fromUsb)
                netlink::provision(line, netOut); // provisioning only over the trusted USB link
            else
                netOut.println("GB_ERROR usb_only");
            return;
        }
        if (!strcmp(line, "calibrate")) {
            calibrationStart();
            return;
        }
        if (!strcmp(line, "calibrate cancel")) {
            calibrationCancel();
            return;
        }
        if (!strcmp(line, "hop") || !strcmp(line, "bounce"))
            startBounce();
        else if (!strcmp(line, "spin"))
            startSpin();
        else if (!strcmp(line, "burst"))
            burst(22);
        else if (!strncmp(line, "sound ", 6)) {
            for (int k = 0; k < kSoundCount; ++k)
                if (!strcmp(line + 6, kSoundNames[k]))
                    playSound(k);
        } else if (!strcmp(line, "blink"))
            blinkNow(millis());
        else if (!strcmp(line, "morph"))
            netOut.printf("GB_MORPH presence=%.3f target=%.0f style=%.3f eye_turn=%.3f\n", blend.x,
                          blend.t, float(state), spin.t);
        else if (!strcmp(line, "shot"))
            dumpFrame();
        else if (!strncmp(line, "rec ", 4)) { // rec <frames> <x0> <y0> <size> <step>
            int n2, x0, y0, sz, st, used = 0;
            if (sscanf(line + 4, "%d %d %d %d %d %n", &n2, &x0, &y0, &sz, &st, &used) != 5 ||
                !badgeprotocol::trailing(line + 4 + used) || n2 < 1 || n2 > 120 || st < 1 ||
                sz < 1 || sz > H || st > sz || sz % st || x0 < 0 || y0 < 0 || x0 > W - sz ||
                y0 > H - sz || uint64_t(n2) * (sz / st) * (sz / st) * 2 > 6 * 1024 * 1024) {
                netOut.println("GB_ERROR bad_rec");
                return;
            }
            const size_t bytes = size_t(n2) * (sz / st) * (sz / st) * 2;
            auto *times = recT ? recT : static_cast<uint32_t *>(ps_malloc(120 * sizeof(uint32_t)));
            if (!times) {
                netOut.println("GB_ERROR rec_memory");
                return;
            }
            if (bytes > recCapacity) {
                auto *buffer = static_cast<uint16_t *>(
                    heap_caps_realloc(recBuf, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (!buffer) {
                    if (!recT)
                        free(times);
                    netOut.println("GB_ERROR rec_memory");
                    return;
                }
                recBuf = buffer;
                recCapacity = bytes;
            }
            recT = times;
            lastInteractionMs = millis();
            recOut = sz / st;
            recN = n2;
            recI = 0;
            recX = x0;
            recY = y0;
            recSize = sz;
            recStep = st;
            Serial.println("GB_REC_START ok=1");
        } else if (!strcmp(line, "recdump"))
            dumpRec();
        else if (!strcmp(line, "next"))
            handleInput(badgeinput::Input::Next);
        else if (!strcmp(line, "prev"))
            handleInput(badgeinput::Input::Prev);
        else if (!strncmp(line, "expr ", 5)) {
            long value;
            if (!badgeprotocol::integer(line + 5, 0, kExprCount - 1, value)) {
                netOut.println("GB_ERROR bad_expr");
                return;
            }
            setExpr(value, 7);
            nextExpr = millis() + 60000;
        } else if (!strncmp(line, "style ", 6)) {
            long value;
            if (!badgeprotocol::integer(line + 6, 0, kStyleCount - 1, value)) {
                netOut.println("GB_ERROR bad_style");
                return;
            }
            lastInteractionMs = millis();
            demoStyle = value;
            applyStyle(demoStyle, true);
        } else if (!strncmp(line, "mood ", 5)) {
            const int s = stateByName(line + 5);
            if (s < 0)
                netOut.printf("GB_ERROR unknown_mood=%s\n", line + 5);
            else if (!assistant.used) {
                lastInteractionMs = millis();
                demoState = s;
                enterState(s, true);
            }
        } else if (!strncmp(line, "look ", 5)) {
            float x, y;
            int used = 0;
            if (sscanf(line + 5, "%f %f %n", &x, &y, &used) != 2 || !isfinite(x) || !isfinite(y) ||
                !badgeprotocol::trailing(line + 5 + used)) {
                netOut.println("GB_ERROR bad_look");
                return;
            }
            gazeX.t = clampf(x, -1, 1) * 15;
            gazeY.t = clampf(y, -1, 1) * 9;
            gazeHoldUntil = millis() + 5000;
        } else if (!strncmp(line, "assistant ", 10))
            handleAssistantLine(line + 10);
        else if (!strncmp(line, "assistant-clear ", 16))
            removeAssistant(line + 16);
        else if (!strcmp(line, "assistant")) {
            netOut.printf("GB_ASSISTANT present=%d id=%s state=%s unseen=%d mood=%s "
                          "manual_sleep=%d demo_style=%d\n",
                          int(assistant.used), assistant.used ? assistant.id : "-",
                          assistant.used ? kAssistantStateNames[int(assistant.state)] : "none",
                          assistant.used && assistant.unseen, kStateNames[state], manualSleep,
                          demoStyle);
        } else if (!strncmp(line, "act ", 4)) {
            const char *a = line + 4;
            if (strcmp(a, "left") && strcmp(a, "right") && strcmp(a, "enter") &&
                strcmp(a, "hold")) {
                netOut.println("GB_ERROR bad_action");
                return;
            }
            handleInput(!strcmp(a, "left")    ? badgeinput::Input::Prev
                        : !strcmp(a, "right") ? badgeinput::Input::Next
                        : !strcmp(a, "enter") ? badgeinput::Input::Enter
                        : !strcmp(a, "hold")  ? badgeinput::Input::Escape
                                              : badgeinput::Input::None);
        } else if (!strcmp(line, "reset")) {
            settingsClose();
            badgereply::cancel();
            calibrationCancel();
            demoState = SIdle;
            demoStyle = 0;
            removeAssistant("*");
            manualSleep = false;
            lastInteractionMs = millis();
            screensaverSince = lastInteractionMs;
            nextScreensaverStep = lastInteractionMs + 12000;
            screensaverSteps = 0;
            spinning = false;
            partCount = 0;
            bounceStart = -1;
            spin.init(0);
            d.init(0);
            o.init(0);
            A.init(0);
            h.init(1);
            lid.init(1);
            eyeScale.init(1);
            gazeX.init(0);
            gazeY.init(0);
            sqx.init(1);
            sqy.init(1);
            dragX.init(0);
            dragY.init(0);
            touchDown = dragging = false;
            pendingExpr = -1;
            lidKeys = 0;
            winkAt = gazeHoldUntil = 0;
            memcpy(sdfCur, kHeadSdf[0], sizeof(sdfCur));
            const RGB color = rgb(kStyles[0].ink);
            inkR.init(color.r);
            inkG.init(color.g);
            inkB.init(color.b);
            applyStyle(0, false);
            state = -1;
            enterState(SIdle, false);
            fullRedraw = true;
        } else
            netOut.println("GB_ERROR unknown_command");
    }
}
