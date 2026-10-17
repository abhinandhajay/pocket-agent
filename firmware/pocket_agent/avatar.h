#pragma once

// ---------- math ----------
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline float frand(float a, float b) { return a + (b - a) * (esp_random() / 4294967295.0f); }
static inline uint32_t urand(uint32_t a, uint32_t b) { return a + uint32_t(frand(0, 1) * (b - a)); }
// sqrt via one-Newton-step inverse sqrt: ~0.2% error, plenty for antialiased coverage.
static inline float fsqrt(float x) {
    if (x <= 0)
        return 0;
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x5f3759df - (u.i >> 1);
    const float y = u.f * (1.5f - 0.5f * x * u.f * u.f);
    return x * y;
}
static RGB rgb(uint32_t hex) {
    return {float((hex >> 16) & 255), float((hex >> 8) & 255), float(hex & 255)};
}

// Damped spring: angular frequency and damping ratio.
struct Spring {
    float x = 0, v = 0, t = 0;
    void init(float value) {
        x = t = value;
        v = 0;
    }
    void step(float w, float z, float h) {
        v += (-2 * z * w * v - w * w * (x - t)) * h;
        x += v * h;
        if (!isfinite(x) || !isfinite(v)) {
            x = t;
            v = 0;
        }
    }
};

// ---------- Assistant state ----------
static constexpr float C = 128.0f;     // original artwork uses a 256-unit canvas
static constexpr float kScale = 0.92f; // screen px per head unit
static constexpr float kHomeX = 234;
// Vertical home: above centre when the name/activity band shows (Assistant), centred without it (demo).
static constexpr float kHomeYBand = 204, kHomeYCentred = 233;
static float kHomeY = kHomeYCentred;                         // eased each tick toward the target
static constexpr uint32_t kBg = 0x000000, kLabel = 0xB9B4A8; // pure black: AMOLED pixels off
struct Style {
    const char *name;
    uint32_t ink;
};
// Diagnostic palette slots share one robot silhouette; the Assistant uses Amber.
static const Style kStyles[] = {
    {"Amber", kRobotShell}, {"Sage", 0xA7D9B7}, {"Coral", 0xFF9B85}, {"Sky", 0x8AC8F5},
    {"Orchid", 0xC9AFF2},   {"Clay", 0xD7AD8B}, {"Rose", 0xF2B5CB},  {"Slate", 0xB2C9DB},
};
static constexpr int kStyleCount = sizeof(kStyles) / sizeof(kStyles[0]);
static const uint32_t kConfetti[] = {0xF9705C, 0x5B95F0, 0x3FBE86, 0xF5B13F, 0x9A72EE, 0x35C3BD};

// ---------- single robot moods ----------
enum EState {
    SIdle,
    SThinking,
    SWorking,
    SWaiting,
    SBlocked,
    SDone,
    SSurprised,
    SSleeping,
    SHappy,
    SCurious,
    SExcited,
    SSad,
    kStateCount
};
static const char *const kStateNames[kStateCount] = {"idle",    "thinking", "working",   "waiting",
                                                     "blocked", "done",     "surprised", "sleeping",
                                                     "happy",   "curious",  "excited",   "sad"};
struct StateDef {
    uint8_t expr[5], nExpr;
    uint16_t exprMin, exprMax, blinkMin, blinkMax;
    float gx0, gx1, gy0, gy1;
    bool gxSigned;
    uint16_t gazeMin, gazeMax;
    uint8_t exprSpeed;
    bool winks;
};
static const StateDef kStates[kStateCount] = {
    // Eye IDs refer to the original expression catalog in robot_art.py.
    /* idle */ {
        {0, 1, 8}, 3, 6500, 12000, 3200, 6500, -.4f, .4f, -.2f, .2f, false, 2200, 4500, 9, true},
    /* thinking */
    {{14, 18, 5}, 3, 1800, 3400, 3000, 5200, .3f, .8f, -.8f, -.3f, true, 1800, 3000, 9, false},
    /* working */
    {{7, 10, 16}, 3, 1600, 2800, 2600, 5000, -.6f, .6f, -.1f, .3f, false, 900, 1700, 11, false},
    /* waiting */
    {{12, 4, 22}, 3, 4000, 7000, 4000, 8000, .3f, .7f, 0, .4f, true, 2600, 4600, 8, false},
    /* blocked */
    {{23, 18, 3}, 3, 2300, 4100, 0, 0, -.2f, .2f, -.1f, .1f, false, 2500, 4500, 10, false},
    /* done */
    {{17, 2, 11}, 3, 1800, 3000, 2200, 4500, -.3f, .3f, -.2f, .2f, false, 1800, 3000, 12, false},
    /* surprised */ {{21, 3}, 2, 2200, 3400, 2000, 4000, 0, 0, 0, 0, false, 1600, 2800, 12, false},
    /* sleeping */ {{13, 22}, 2, 7000, 11000, 0, 0, 0, 0, 0, 0, false, 3000, 5000, 8, false},
    /* happy */
    {{2, 11, 19, 6, 9},
     5,
     2200,
     4000,
     3000,
     5500,
     -.5f,
     .5f,
     -.3f,
     .1f,
     false,
     1800,
     3500,
     11,
     true},
    /* curious */
    {{15, 12, 18, 8}, 4, 1600, 2900, 2500, 4800, .4f, 1, -.7f, .7f, true, 1100, 2100, 10, true},
    /* excited */
    {{17, 21, 2, 11}, 4, 1100, 1900, 2400, 4300, -.8f, .8f, -.6f, .2f, false, 700, 1400, 13, true},
    /* sad */
    {{24, 23, 20}, 3, 4200, 7000, 4500, 8000, -.2f, .2f, .3f, .7f, false, 2800, 4500, 8, false},
};
static int state = SIdle;
static int demoState = SIdle; // demo look, restored when the Assistant leaves
static int demoStyle = 0;
static int styleIndex = 0;

// ---------- animation state ----------
static Spring d, o, A, h;    // rotation (deg), x offset, y offset (units), vertical scale
static Spring lid, eyeScale; // eyelid openness, eye size
static Spring gazeX, gazeY;  // eye offset (units, +-15 / +-9)
static Spring blend;         // expression morph 0 -> 1
static Spring spin;          // whole-shell playful roll, never a disappearing face
static Spring inkR, inkG, inkB;
static Spring sqx, sqy, dragX, dragY;
static bool spinning = false;
static int pendingExpr = -1;
static uint32_t pendingAt = 0;
static uint8_t pendingSpeed = 6;
static int8_t sdfCur[kSdfN * kSdfN];
// Active original eye contours live in internal RAM for bounded render time.
static int8_t eyeTex[2][7680];
static int eyeTexW[2], eyeTexH[2], eyeTexExpr = -1;
static float eyeTexX0[2], eyeTexY0[2];
static void loadEyeTex(int e) {
    if (e == eyeTexExpr)
        return;
    for (int i = 0; i < 2; ++i) {
        const int32_t *inf = kEyeTexInfo[e][i];
        eyeTexW[i] = inf[1];
        eyeTexH[i] = inf[2];
        eyeTexX0[i] = inf[3];
        eyeTexY0[i] = inf[4];
        memcpy(eyeTex[i], kEyeTex + inf[0], size_t(inf[1]) * inf[2]);
    }
    eyeTexExpr = e;
}
static inline float sampleEyeTex(int i, float x, float y, float outsideDistance) {
    const float fx = x - eyeTexX0[i], fy = y - eyeTexY0[i];
    const int w = eyeTexW[i], h = eyeTexH[i];
    if (fx < 0 || fy < 0 || fx >= w - 1 || fy >= h - 1)
        return outsideDistance;
    const int ix = int(fx), iy = int(fy);
    const float tx = fx - ix, ty = fy - iy;
    const int8_t *p = eyeTex[i] + iy * w + ix;
    const float a = p[0] + (p[1] - p[0]) * tx, b = p[w] + (p[w + 1] - p[w]) * tx;
    return (a + (b - a) * ty) * (1 / kSdfQuant);
}
static Cap exprFrom[2], exprTo[2];
static int exprIndex = 0;
static uint8_t exprSpeed = 6;
static uint32_t stateSince = 0, nextExpr = 0, nextBlink = 0, nextGaze = 0, nextSpecial = 0,
                nextWink = 0;
static uint32_t gazeHoldUntil = 0;
static uint32_t lidKeyAt[6];
static float lidKeyV[6];
static int lidKeys = 0;
static uint32_t winkAt = 0;
static int winkEye = 0;
static int32_t bounceStart = -1;
static bool badge = false;
static Particle parts[28];
static int partCount = 0;
static uint32_t labelUntil = 0;
static char labelText[32] = "";

// Touch
static bool touchDown = false, dragging = false;
static float touchX = 0, touchY = 0, touchStartX = 0, touchStartY = 0;
static uint32_t touchStartMs = 0, lastTouchMs = 0;
static float grabDX = 0, grabDY = 0;

// IMU
static float tiltX = 0, tiltY = 0;
static bool imuOk = false;
static uint32_t lastImuLog = 0;

static badgeinput::ButtonGrammar buttons;

static void showLabel(const char *text) {
    strncpy(labelText, text, sizeof(labelText) - 1);
    labelUntil = millis() + 1600;
}

static Cap exprCap(int e, int eye) {
    const float *v = kExpr[e][eye];
    return {v[0], v[1], v[2], v[3], v[4]};
}

// Current morphed expression (angles take the nearest equivalent: capsules are symmetric).
static Cap blendedCap(int eye) {
    const float t = clampf(blend.x, 0, 1);
    const Cap &a = exprFrom[eye], &b = exprTo[eye];
    float db = b.ang - a.ang;
    while (db > PI / 2)
        db -= PI;
    while (db < -PI / 2)
        db += PI;
    return {mixf(a.cx, b.cx, t), mixf(a.cy, b.cy, t), a.ang + db * t, mixf(a.len, b.len, t),
            mixf(a.wid, b.wid, t)};
}

// Swap expressions during a short blink so contour changes stay gentle.
static void blinkNow(uint32_t now);
static void changeExpr(int e, uint8_t speed, uint32_t now) {
    // A later mood must cancel an expression still waiting behind an earlier blink.
    pendingExpr = -1;
    if (e == exprIndex)
        return;
    blinkNow(now);
    pendingExpr = e;
    pendingSpeed = speed;
    pendingAt = now + 90;
}
static void setExpr(int e, uint8_t speed) {
    if (e == exprIndex && blend.x > 0.99f)
        return;
    exprFrom[0] = blendedCap(0);
    exprFrom[1] = blendedCap(1);
    exprTo[0] = exprCap(e, 0);
    exprTo[1] = exprCap(e, 1);
    exprIndex = e;
    exprSpeed = speed;
    blend.x = 0;
    blend.v = 0;
    blend.t = 1;
}

static void blinkNow(uint32_t now) {
    lidKeys = 3;
    lidKeyAt[0] = now;
    lidKeyAt[1] = now + 105;
    lidKeyAt[2] = now + 235;
    lidKeyV[0] = .07f;
    lidKeyV[1] = .07f;
    lidKeyV[2] = 1;
}

static void startBounce() {
    if (bounceStart < 0)
        bounceStart = int32_t(millis());
}
static void startSpin() {
    if (spinning || state == SSleeping)
        return;
    spinning = true;
    spin.init(0);
    spin.t = 2 * PI * (frand(0, 1) < .5f ? 1 : -1);
}
static void burst(int n) {
    for (int i = 0; i < n && partCount < 28; ++i) {
        const float a = frand(-PI, 0), sp = frand(220, 520);
        parts[partCount++] = {kHomeX + dragX.x,
                              kHomeY + dragY.x - 60,
                              cosf(a) * sp,
                              sinf(a) * sp - 120,
                              frand(0, 6.3f),
                              frand(-9, 9),
                              0,
                              frand(9, 15),
                              kConfetti[esp_random() % 6]};
    }
}

static void applyStyle(int index, bool announce) {
    styleIndex = (index % kStyleCount + kStyleCount) % kStyleCount;
    const Style &s = kStyles[styleIndex];
    const RGB c = rgb(s.ink);
    inkR.t = c.r;
    inkG.t = c.g;
    inkB.t = c.b;
    sqy.v -= 3.5f;
    sqx.v += 3.0f;
    if (announce)
        showLabel(s.name);
    netOut.printf("GB_EVENT style=%s\n", s.name);
}

static void enterState(int s, bool announce) {
    if (s == state)
        return;
    state = s;
    const StateDef &def = kStates[s];
    const uint32_t now = millis();
    stateSince = now;
    changeExpr(def.expr[esp_random() % def.nExpr], def.exprSpeed, now);
    nextExpr = now + urand(def.exprMin, def.exprMax);
    nextBlink = now + urand(1500, 7000);
    nextGaze = now + urand(500, 1400);
    nextSpecial = now + (s == SExcited   ? urand(400, 1100)
                         : s == SWorking ? urand(1200, 2400)
                                         : urand(9000, 18000));
    nextWink = now + urand(3000, 8000);
    // Brightness is managed centrally in updateBrightness().
    if (s == SDone) {
        startBounce();
        burst(18);
    }
    if (s == SSleeping) {
        spinning = false;
        spin.init(0);
    }
    if (announce)
        showLabel(kStateNames[s]);
    netOut.printf("GB_EVENT mood=%s\n", kStateNames[s]);
}

// ---------- transition sounds (synthesized at boot, 22.05 kHz mono) ----------
enum SoundId { SndPop, SndStart, SndChime, SndWince, SndUhoh, kSoundCount };
static const char *const kSoundNames[kSoundCount] = {"pop", "start", "chime", "wince", "uhoh"};
static int16_t *soundPcm[kSoundCount];
static size_t soundLen[kSoundCount];
static constexpr uint32_t kSoundRate = 22050;
static bool soundMuted(); // manual sleep mutes

// A tone with a short attack and exponential decay; partials add bell colour.
static void addTone(float *buf, size_t n, float t0, float dur, float f0, float f1, float amp,
                    float tau, float partial = 0, float partialAmp = 0) {
    const size_t a = size_t(t0 * kSoundRate), len = size_t(dur * kSoundRate);
    float ph = 0, ph2 = 0;
    for (size_t i = 0; i < len && a + i < n; ++i) {
        const float t = float(i) / kSoundRate, k = float(i) / len;
        const float f = f0 + (f1 - f0) * k;
        ph += 2 * PI * f / kSoundRate;
        ph2 += 2 * PI * f * partial / kSoundRate;
        const float env =
            fminf(t / .004f, 1) * expf(-t / tau) * fminf((len - i) / (.006f * kSoundRate), 1);
        buf[a + i] += amp * env * (sinf(ph) + partialAmp * sinf(ph2));
    }
}
static void makeSound(int id, float seconds, void (*fill)(float *, size_t)) {
    const size_t n = size_t(seconds * kSoundRate);
    float *tmp = static_cast<float *>(ps_malloc(n * sizeof(float)));
    soundPcm[id] = static_cast<int16_t *>(ps_malloc(n * sizeof(int16_t)));
    if (!tmp || !soundPcm[id]) {
        soundLen[id] = 0;
        free(tmp);
        return;
    }
    memset(tmp, 0, n * sizeof(float));
    fill(tmp, n);
    float peak = 1e-6f;
    for (size_t i = 0; i < n; ++i)
        peak = fmaxf(peak, fabsf(tmp[i]));
    for (size_t i = 0; i < n; ++i)
        soundPcm[id][i] = int16_t(tmp[i] / peak * 15000); // ~-6.8 dBFS
    soundLen[id] = n;
    free(tmp);
}
static void initSounds() {
    makeSound(SndPop, .09f,
              [](float *b, size_t n) { addTone(b, n, 0, .09f, 520, 1100, 1, .035f); });
    makeSound(SndStart, .05f,
              [](float *b, size_t n) { addTone(b, n, 0, .05f, 1760, 1900, 1, .012f); });
    makeSound(SndChime, .75f, [](float *b, size_t n) { // C6 E6 G6 bell arpeggio
        addTone(b, n, 0.00f, .60f, 1046.5f, 1046.5f, 1, .20f, 2.76f, .25f);
        addTone(b, n, 0.09f, .60f, 1318.5f, 1318.5f, .9f, .20f, 2.76f, .25f);
        addTone(b, n, 0.18f, .57f, 1568.0f, 1568.0f, .85f, .24f, 2.76f, .25f);
    });
    makeSound(SndWince, .13f,
              [](float *b, size_t n) { addTone(b, n, 0, .13f, 470, 330, 1, .06f, 2, .3f); });
    makeSound(SndUhoh, .42f, [](float *b, size_t n) { // "uh-oh": G4 then D4
        addTone(b, n, 0.00f, .15f, 392, 392, 1, .10f, 3, .25f);
        addTone(b, n, 0.18f, .24f, 294, 290, 1, .14f, 3, .25f);
    });
}
static void playSound(int id) {
    if (soundMuted() || id < 0 || id >= kSoundCount || !soundLen[id])
        return;
    M5.Speaker.playRaw(soundPcm[id], soundLen[id], kSoundRate, false, 1, 0, true);
    netOut.printf("GB_SOUND %s\n", kSoundNames[id]);
}
