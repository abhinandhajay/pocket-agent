#pragma once

// ---------- input ----------
static void wake() {
    manualSleep = false;
    if (!assistant.used && state == SSleeping) {
        demoState = SIdle;
        enterState(SIdle, false);
    }
}

static bool settingsOpen() {
    if (calibration.active() || badgevoice::recording || badgevoice::sending) {
        netOut.println("GB_ERROR settings_busy");
        return false;
    }
    if (settings.visible)
        return true;
    badgereply::cancel();
    M5.Speaker.stop();
    wake();
    settings.open();
    settingsDirty = true;
    settingsTouchDrain = true;
    touchDown = dragging = false;
    lastInteractionMs = millis();
    netOut.println("GB_EVENT settings_open");
    return true;
}
static void settingsClose() {
    if (!settings.visible)
        return;
    settings.close();
    fullRedraw = true;
    legendValid = false;
    touchDown = dragging = false;
    lastInteractionMs = millis();
    netOut.println("GB_EVENT settings_close");
}
static void settingsActivate() {
    if (!settings.activate())
        return;
    if (settings.muted)
        M5.Speaker.stop();
    M5.Speaker.setVolume(settings.muted ? 0 : 170);
    Preferences p;
    bool saved = p.begin("agent-ui", false);
    if (saved) {
        saved = p.putBool("mute", settings.muted) == 1 &&
                p.putUChar("brightness", settings.brightness) == 1;
        p.end();
    }
    if (!saved)
        netOut.println("GB_ERROR settings_save");
    lastInteractionMs = millis();
    settingsInfo();
}
static badgesettings::Snapshot settingsSnapshot() {
    static int level = -1, charge = -1;
    static uint32_t sampled = 0;
    const uint32_t now = millis();
    if (!sampled || now - sampled >= 2000) {
        level = M5.Power.getBatteryLevel();
        if (level < 0 || level > 100)
            level = -1;
        const auto c = M5.Power.isCharging();
        charge = c == m5::Power_Class::is_charging      ? 1
                 : c == m5::Power_Class::is_discharging ? 0
                                                        : -1;
        sampled = now;
    }
    badgesettings::Snapshot s;
    s.battery = level;
    s.charging = charge;
    s.wifiSaved = netlink::wifiSsid[0] != 0;
    s.wifiConnected = WiFi.status() == WL_CONNECTED;
    s.rssi = s.wifiConnected ? WiFi.RSSI() : 0;

    if (assistant.used && uint32_t(now - lastHubMs) <= 20000) {
        if (!strcmp(badgevoice::channel, "usb") && Serial)
            s.link = badgesettings::Link::Usb;
        else if (!strcmp(badgevoice::channel, "wifi") && netlink::linkUp)
            s.link = badgesettings::Link::Wifi;
        s.voiceCapable = assistant.voice;
        s.voiceBusy = busy(assistant.state) || assistant.state == AssistantState::Waiting ||
                      badgevoice::recording || badgevoice::sending;
        s.voiceReady = s.voiceCapable && badgevoice::ready() && !s.voiceBusy;
    }
    return s;
}
static void settingsInfo() {
    const auto s = settingsSnapshot();
    netOut.printf("GB_SETTINGS open=%d row=%u battery=%d charging=%d wifi=%s rssi=%d "
                  "agent=%s voice=%s sound=%s brightness=%d\n",
                  settings.visible, unsigned(settings.row), s.battery, s.charging,
                  s.wifiConnected ? "connected"
                  : s.wifiSaved   ? "connecting"
                                  : "none",
                  s.rssi, badgesettings::agent(s), badgesettings::voice(s),
                  settings.muted ? "muted" : "on", badgesettings::percent(settings.brightness));
}
static Rect renderSettings() {
    bandDirty = {};
    static char previous[badgesettings::kRows][32]{};
    static int selected = -1;
    Rect dirty{W, H, 0, 0};
    static badgesettings::Snapshot s;
    static uint32_t statusAt = 0;
    if (settingsDirty || uint32_t(millis() - statusAt) >= 500) {
        s = settingsSnapshot();
        statusAt = millis();
    }
    if (settingsDirty) {
        canvas.fillScreen(0);
        canvas.setFont(&fonts::FreeSansBold18pt7b);
        canvas.setTextSize(1);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("Settings", 234, 63);
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        canvas.setTextColor(0x8090A0U);
        canvas.drawString("Left/right choose  -  both change", 234, 94);
        canvas.drawString("Hold both or tap Close to return", 234, 387);
        canvas.drawRoundRect(158, 397, 152, 31, 12, 0x3A4653U);
        canvas.setFont(&fonts::Font2);
        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("Close", 234, 412);
        dirty = {0, 0, W, H};
    }
    for (int row = 0; row < badgesettings::kRows; ++row) {
        char value[32];
        badgesettings::value(s, settings, row, value, sizeof(value));
        if (!settingsDirty && !strcmp(value, previous[row]) && selected == settings.row)
            continue;
        if (!settingsDirty && !strcmp(value, previous[row]) && row != selected &&
            row != settings.row)
            continue;
        strcpy(previous[row], value);
        const int y = badgesettings::kTop + row * badgesettings::kPitch;
        canvas.fillRoundRect(68, y, 332, 34, 8, 0);
        canvas.drawRoundRect(68, y, 332, 34, 8, row == settings.row ? 0x1084FEU : 0x242B33U);
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(2);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(row >= badgesettings::kSound ? TFT_WHITE : 0xA4ABB4U);
        canvas.drawString(badgesettings::labels[row], 80, y + 17);
        canvas.setTextDatum(textdatum_t::middle_right);
        canvas.setTextColor(TFT_WHITE);
        canvas.drawString(value, 388, y + 17);
        if (!settingsDirty)
            dirty = {min(dirty.x0, 68), min(dirty.y0, y), max(dirty.x1, 400),
                     max(dirty.y1, y + 34)};
    }
    selected = settings.row;
    settingsDirty = false;
    return dirty.x1 > dirty.x0 ? dirty : Rect{0, 0, 0, 0};
}
static void settingsTouch(uint32_t now) {
    static bool down = false;
    static touchcal::Point start{}, last{};
    static uint32_t at = 0;
    lgfx::touch_point_t raw;
    if (!touchMapVersion)
        return;
    const bool pressed = M5.Display.getTouchRaw(&raw) > 0;
    if (settingsTouchDrain) {
        down = false;
        if (!pressed)
            settingsTouchDrain = false;
        return;
    }
    if (pressed) {
        last = touchcal::applyWarp(touchMap, {double(raw.x), double(raw.y)});
        if (!down) {
            down = true;
            start = last;
            at = now;
        }
        lastInteractionMs = now;
    } else if (down) {
        down = false;
        if (now - at >= 350 || hypot(last.x - start.x, last.y - start.y) >= 14)
            return;
        const int row = badgesettings::hit(last.x, last.y);
        if (row == -2)
            settingsClose();
        else if (row >= 0) {
            settings.row = row;
            settingsActivate();
        }
    }
}
