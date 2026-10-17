// Pocket Agent for the M5Stack C152.
// One original rounded robot, 25 eye expressions and twelve animated moods.
// The host supplies one Assistant conversation. Left/right acknowledge or page
// replies, both briefly acknowledge, and a 600 ms chord sleeps. Hold blue to talk.
// Touch the robot to bounce or drag it, or tap elsewhere to steer its gaze.
#pragma GCC optimize("O2")
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "buttons.h"
#include "protocol_numbers.h"
#include "types.h"
#include "touch_calibration_core.h"
#include "avatar_assets.h"
#include "net_link.h"
#include "voice_io.h"
#include "reply_io.h"
#include "settings_core.h"

static badgesettings::Model settings;
static badgesettings::LeftHold settingsHold;
static bool settingsDirty = true, settingsTouchDrain = false;
static bool settingsOpen();
static void settingsClose();
static void settingsInfo();

static constexpr int W = 468, H = 466;
static M5Canvas canvas(&M5.Display);
static uint16_t *fb = nullptr;

// ---------- touch map (shared espt-touch/record) ----------
static touchcal::WarpMap touchMap{};
static uint16_t touchMapVersion = 0;
static uint32_t touchMapGeneration = 0;

static bool loadTouchMap() {
    Preferences prefs;
    if (!prefs.begin(touchcal::kNvsNamespace, true))
        return false;
    const size_t length = prefs.getBytesLength("record");
    touchcal::RecordV2 v2{};
    touchcal::Record v1{};
    size_t read = 0;
    if (length == sizeof(v2))
        read = prefs.getBytes("record", &v2, sizeof(v2));
    else if (length == sizeof(v1))
        read = prefs.getBytes("record", &v1, sizeof(v1));
    prefs.end();
    if (read == sizeof(v2) && touchcal::validateRecordV2(v2)) {
        touchMap.affine = {v2.affine[0], v2.affine[1], v2.affine[2],
                           v2.affine[3], v2.affine[4], v2.affine[5]};
        touchMap.center = {v2.centerResidual[0], v2.centerResidual[1]};
        for (size_t r = 0; r < touchcal::kRadialRingCount; ++r)
            for (size_t s = 0; s < touchcal::kPerimeterSectorCount; ++s)
                touchMap.rings[r][s] = {v2.residuals[r][s][0], v2.residuals[r][s][1]};
        touchMapVersion = 2;
        touchMapGeneration = v2.generation;
        return true;
    }
    if (read == sizeof(v1) && touchcal::validateRecord(v1)) {
        touchMap = touchcal::warpFromV1(v1);
        touchMapVersion = 1;
        return true;
    }
    return false;
}

#include "touch_calibration_ui.h"

// Application modules share this sketch translation unit in dependency order.
#include "avatar.h"
#include "assistant.h"
#include "animation.h"
#include "renderer.h"
#include "settings_ui.h"
#include "input.h"
#include "diagnostics.h"

// ---------- setup / loop ----------
void setup() {
    Serial.begin(115200);
    Serial.setRxBufferSize(8192);
    // Never block on USB: with the host port closed (hub released it, or a charger-only
    // connection) CDC writes would otherwise wait out a timeout per call and stall the loop.
    Serial.setTxTimeoutMs(0);
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_rtc = false;
    cfg.internal_mic = true;
    cfg.internal_spk = true;
    cfg.fallback_board = m5::board_t::board_M5StopWatch;
    M5.begin(cfg);
    M5.BtnB.setHoldThresh(badgevoice::kHoldMs);
    M5.Mic.end();
    badgevoice::beforeStart = badgereply::cancel;
    badgevoice::linkChanged = badgereply::cancel;
    Serial.setTxBufferSize(8192);
    auto *panel = M5.Display.getPanel();
    auto pc = panel->config();
    pc.panel_width = 468;
    pc.panel_height = 466;
    pc.offset_x = 6;
    pc.offset_y = 0;
    panel->config(pc);
    M5.Display.setRotation(0);
    M5.Display.setBrightness(255);
    Preferences uiPrefs;
    if (uiPrefs.begin("agent-ui", true)) {
        settings.load(uiPrefs.getBool("mute", false), uiPrefs.getUChar("brightness", 255));
        uiPrefs.end();
    }
    netOut.printf("GB_HARDWARE board=%d expected=%d size=%dx%d touch=%d psram=%u\n",
                  int(M5.getBoard()), int(m5::board_t::board_M5StopWatch), int(M5.Display.width()),
                  int(M5.Display.height()), M5.Touch.isEnabled(), unsigned(ESP.getPsramSize()));
    const bool mapOk = loadTouchMap();
    netOut.printf("GB_TOUCHMAP ok=%d version=%u generation=%u\n", mapOk, unsigned(touchMapVersion),
                  unsigned(touchMapGeneration));
    imuOk = M5.Imu.isEnabled();
    const bool spk = M5.Speaker.begin();
    M5.Speaker.setVolume(settings.muted ? 0 : 170);
    initSounds();
    netOut.printf("GB_SPEAKER ok=%d\n", spk);
    netlink::begin();
    netlink::hello(netOut);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    fb = static_cast<uint16_t *>(canvas.createSprite(W, H));
    if (!fb) {
        netOut.println("GB_FATAL canvas");
        while (true)
            delay(1000);
    }

    d.init(0);
    o.init(0);
    A.init(0);
    h.init(1);
    lid.init(1);
    eyeScale.init(1);
    gazeX.init(0);
    gazeY.init(0);
    blend.init(1);
    spin.init(0);
    sqx.init(1);
    sqy.init(1);
    dragX.init(0);
    dragY.init(0);
    const RGB c = rgb(kStyles[0].ink);
    inkR.init(c.r);
    inkG.init(c.g);
    inkB.init(c.b);
    memcpy(sdfCur, kHeadSdf[0], sizeof(sdfCur));
    exprTo[0] = exprFrom[0] = exprCap(0, 0);
    exprTo[1] = exprFrom[1] = exprCap(0, 1);
    state = -1;
    enterState(SIdle, false);
    sqx.x = 1.3f;
    sqy.x = .7f; // entrance pop
    startBounce();
    showLabel(kStyles[0].name);
    netOut.println("GB_READY");
    if (!mapOk)
        calibrationStart();
}

void loop() {
    static uint32_t last = millis(), fpsStart = millis(), frames = 0, renderUs = 0, busyUs = 0;
    M5.update();
    const uint32_t now = millis();
    const float dt = clampf((now - last) / 1000.0f, .001f, .05f);
    last = now;

    handleSerial();
    netlink::poll();
    badgevoice::poll();
    badgereply::poll(assistant.used ? assistant.id : "",
                     !settings.visible && !manualSleep && !calibration.active());
    static bool replyWasVisible = false;
    if (replyWasVisible && !badgereply::visible) {
        fullRedraw = true;
        touchDown = dragging = false;
    }
    replyWasVisible = badgereply::visible;
    static const uint32_t bootInputStart = now;
    static uint32_t bootChord = 0;
    static bool bootChecked = false, bootOwnsChord = false;
    if (!bootChecked && now - bootInputStart < 2000) {
        if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
            bootOwnsChord = true;
            if (!bootChord)
                bootChord = now;
            if (now - bootChord >= 600) {
                calibrationStart();
                bootChecked = true;
            }
        } else
            bootChord = 0;
    } else
        bootChecked = true;
    if (bootOwnsChord) {
        // Consume this whole chord so entering calibration cannot also sleep the
        // assistant or cancel the collector on the same 600 ms hold.
        buttons.update(now, M5.BtnA.isPressed(), M5.BtnB.isPressed(), M5.BtnA.wasClicked(),
                       M5.BtnB.wasClicked());
        if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed())
            bootOwnsChord = false;
    } else
        handleButtons(now);
    if (calibration.active()) {
        settingsClose();
        calibrationTick(now);
        calibrationDraw();
        return;
    }
    if (settings.visible)
        settingsTouch(now);
    else if (badgereply::visible)
        replyTouch();
    else
        handleTouch(now);
    handleImu(now);
    tick(dt, now);

    const uint32_t t0 = micros();
    const Rect r = settings.visible      ? renderSettings()
                   : badgereply::visible ? renderReply()
                                         : render(now);
    renderUs += micros() - t0;
    if (r.x1 > r.x0 && r.y1 > r.y0) {
        M5.Display.setClipRect(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
        canvas.pushSprite(0, 0);
    }
    if (bandDirty.y1 > bandDirty.y0 && !(r.x0 == 0 && r.y0 == 0 && r.x1 == W && r.y1 == H)) {
        M5.Display.setClipRect(bandDirty.x0, bandDirty.y0, bandDirty.x1 - bandDirty.x0,
                               bandDirty.y1 - bandDirty.y0);
        canvas.pushSprite(0, 0);
    }
    M5.Display.clearClipRect();
    busyUs += micros() - t0;
    recordFrame(now);

    if (++frames, now - fpsStart >= 5000) {
        netOut.printf("GB_FPS fps=%.1f frame_ms=%.1f render_ms=%.1f fill=%.1f tiles=%.1f "
                      "extra=%.1f shaded=%u poly=%u heap=%u stack_free=%u\n",
                      frames * 1000.0f / (now - fpsStart), busyUs / 1000.0f / frames,
                      renderUs / 1000.0f / frames, profFill / 1000.0f / frames,
                      profTiles / 1000.0f / frames, profExtra / 1000.0f / frames,
                      unsigned(profShaded / frames), unsigned(profPoly / frames),
                      unsigned(ESP.getFreeHeap()), unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        frames = 0;
        renderUs = 0;
        busyUs = 0;
        fpsStart = now;
        profFill = profTiles = profExtra = profShaded = profPoly = 0;
    }
    if (settings.visible)
        delay(5);
}
