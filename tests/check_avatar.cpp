// Exercise the actual robot, motion and renderer with an in-memory display.
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include "types.h"
#include "buttons.h"
#include "touch_calibration_core.h"
#include "avatar_assets.h"
using std::isfinite;
using std::max;
using std::min;
static constexpr float PI = 3.14159265358979323846f;
static constexpr int W = 468, H = 466;
static std::array<uint16_t, W * H + 32> pixels;
static uint16_t *fb = pixels.data() + 16;
static uint32_t clockMs = 1000;
static uint32_t millis() { return clockMs; }
static uint32_t micros() { return clockMs * 1000; }
static uint32_t esp_random() {
    static uint32_t seed = 1701;
    seed = seed * 1664525U + 1013904223U;
    return seed;
}
static void *ps_malloc(size_t bytes) { return malloc(bytes); }
static struct {
    void println(const char *) {}
    template <typename... T> void printf(const char *, T...) {}
} netOut;
namespace fonts {
static int FreeSansBold12pt7b, FreeSans9pt7b, Font2;
}
namespace textdatum_t {
enum { middle_center, top_left };
}
namespace lgfx {
struct touch_point_t {
    int x, y;
};
} // namespace lgfx
static struct {
    struct {
        int getTouchRaw(lgfx::touch_point_t *) { return 0; }
        void setBrightness(int) {}
    } Display;
    struct {
        template <typename... T> void playRaw(T...) {}
    } Speaker;
} M5;
static touchcal::WarpMap touchMap;
static int touchMapVersion = 0;
static struct {
    bool visible = false, muted = true;
    int brightness = 255;
} settings;
namespace badgevoice {
static bool recording = false;
static void cancel() {}
static const char *activity(const char *) { return nullptr; }
} // namespace badgevoice
namespace badgereply {
static struct {
    char text[4097]{};
    bool receiving = false;
} rx;
static uint32_t uiRevision = 0;
static char rid[17]{};
static uint16_t pages = 1, page = 0, pageOffsets[349]{};
static void cancel() {}
} // namespace badgereply
static struct {
    static uint16_t swapped(uint32_t color) {
        uint16_t value = color > 0xFFFF ? uint16_t(((color >> 19) & 31) << 11 |
                                                   ((color >> 10) & 63) << 5 | ((color >> 3) & 31))
                                        : uint16_t(color);
        return uint16_t((value >> 8) | (value << 8));
    }
    void fillRect(int x, int y, int width, int height, uint32_t color) {
        for (int yy = max(0, y); yy < min(H, y + height); ++yy)
            for (int xx = max(0, x); xx < min(W, x + width); ++xx)
                fb[yy * W + xx] = swapped(color);
    }
    void fillSprite(uint32_t color) { fillRect(0, 0, W, H, color); }
    void fillSmoothCircle(float x, float y, float radius, uint32_t color) {
        for (int yy = max(0, int(y - radius - 1)); yy < min(H, int(y + radius + 2)); ++yy)
            for (int xx = max(0, int(x - radius - 1)); xx < min(W, int(x + radius + 2)); ++xx)
                if (hypotf(xx + .5f - x, yy + .5f - y) <= radius)
                    fb[yy * W + xx] = swapped(color);
    }
    void fillCircle(int x, int y, int radius, uint32_t color) {
        fillSmoothCircle(x, y, radius, color);
    }
    void drawWideLine(float x, float y, float x1, float y1, float width, uint32_t color) {
        const int steps = max(1, int(ceilf(hypotf(x1 - x, y1 - y))));
        for (int i = 0; i <= steps; ++i)
            fillSmoothCircle(x + (x1 - x) * i / steps, y + (y1 - y) * i / steps, width, color);
    }
    void setFont(const void *) {}
    void setTextDatum(int) {}
    void setTextSize(float) {}
    void setTextColor(uint32_t) {}
    int textWidth(const char *text) { return int(strlen(text)) * 8; }
    void drawString(const char *, int, int) {}
} canvas;
static constexpr int TFT_BLACK = 0, TFT_WHITE = 0xFFFF;
#include "avatar.h"
#include "assistant.h"
#include "animation.h"
#include "renderer.h"

static void resetVisual() {
    pixels.fill(0x1234);
    std::fill(fb, fb + W * H, 0);
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
    blend.init(1);
    spin.init(0);
    const RGB shell = rgb(kRobotShell);
    inkR.init(shell.r);
    inkG.init(shell.g);
    inkB.init(shell.b);
    memcpy(sdfCur, kHeadSdf[0], sizeof(sdfCur));
    assistant = Assistant{};
    manualSleep = false;
    lastInteractionMs = clockMs;
    screensaverSince = nextScreensaverStep = 0;
    screensaverSteps = 0;
    kHomeY = kHomeYCentred;
    pendingExpr = -1;
    partCount = lidKeys = 0;
    spinning = badge = false;
    winkAt = labelUntil = 0;
    bounceStart = -1;
    fullRedraw = true;
}
static void settledExpression(int expression) {
    exprIndex = expression;
    exprFrom[0] = exprTo[0] = exprCap(expression, 0);
    exprFrom[1] = exprTo[1] = exprCap(expression, 1);
    blend.init(1);
}
static void checkBounds(Rect r) {
    assert(r.x0 >= 0 && r.y0 >= 0 && r.x1 <= W && r.y1 <= H);
    assert(r.x0 <= r.x1 && r.y0 <= r.y1);
    for (int i = 0; i < 16; ++i)
        assert(pixels[i] == 0x1234 && pixels[W * H + 16 + i] == 0x1234);
}
static void saveFrame(const std::filesystem::path &path) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << W << " " << H << "\n255\n";
    for (int i = 0; i < W * H; ++i) {
        uint16_t c = unswap(fb[i]);
        const unsigned char rgb[] = {static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
                                     static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63),
                                     static_cast<unsigned char>((c & 31) * 255 / 31)};
        out.write(reinterpret_cast<const char *>(rgb), 3);
    }
    assert(out.good());
}
int main(int argc, char **argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "";
    if (!output.empty())
        std::filesystem::create_directories(output);
    assert(kHeadCount == 1 && kExprCount == 25 && kStateCount == 12);
    for (int expression = 0; expression < kExprCount; ++expression) {
        resetVisual();
        state = SIdle;
        settledExpression(expression);
        for (int i = 0; i < 2; ++i) {
            const auto *info = kEyeTexInfo[expression][i];
            assert(info[0] >= 0 && info[1] > 1 && info[2] > 1);
            assert(info[1] * info[2] <= 7680);
            assert(size_t(info[0] + info[1] * info[2]) <= sizeof(kEyeTex));
        }
        checkBounds(render(clockMs));
        const RGB eyes = rgb(kRobotEyes);
        assert(std::count(fb, fb + W * H, pack(eyes.r, eyes.g, eyes.b)) > 20);
        const RGB visor = rgb(kRobotVisor);
        for (int side : {-1, 1}) {
            const int x = int(kHomeX + side * 57 * kScale);
            const int y = int(kHomeY + 20 * kScale);
            assert(fb[y * W + x] == pack(visor.r, visor.g, visor.b));
        }
        // The original three-dot mouth sits in the face, with no extra mouth or chin dots.
        for (int dot = -1; dot <= 1; ++dot) {
            const int x = int(kHomeX + dot * 13 * kScale);
            const int y = int(kHomeY + 39 * kScale);
            assert(fb[y * W + x] != pack(visor.r, visor.g, visor.b));
        }
        assert(fb[int(kHomeY + 31 * kScale) * W + int(kHomeX)] == pack(visor.r, visor.g, visor.b));
        const RGB shell = rgb(kRobotShell);
        assert(fb[int(kHomeY + 74 * kScale) * W + int(kHomeX)] == pack(shell.r, shell.g, shell.b));
        if (!output.empty())
            saveFrame(output / ("expr-" + std::to_string(expression) + ".ppm"));
        if (expression == 0) {
            // Measure the actual rendered rim away from the antenna and ear tabs.
            const uint16_t shellInk = pack(shell.r, shell.g, shell.b);
            int rim[4]{};
            for (int y = 0; y < H; ++y)
                if (fb[y * W + int(kHomeX)] == shellInk)
                    ++rim[y < kHomeY ? 0 : 1];
            const int sideY = int(kHomeY + 30 * kScale);
            for (int x = 0; x < W; ++x)
                if (fb[sideY * W + x] == shellInk)
                    ++rim[x < kHomeX ? 2 : 3];
            assert(*std::min_element(rim, rim + 4) > 10);
            assert(*std::max_element(rim, rim + 4) - *std::min_element(rim, rim + 4) <= 1);
            printf("Rendered border: top=%d bottom=%d left=%d right=%d pixels.\n", rim[0], rim[1],
                   rim[2], rim[3]);
            const uint16_t highlight = pack(248, 255, 245);
            assert(std::count(fb, fb + W * H, highlight) == 0);
            lid.init(.08f);
            checkBounds(render(clockMs));
            assert(std::count(fb, fb + W * H, highlight) == 0);
            lid.init(1);
            checkBounds(render(clockMs));
        }
        // A retained frame after movement must match a fresh full redraw exactly.
        dragX.init(95);
        dragY.init(-40);
        d.init(18);
        lid.init(.08f);
        checkBounds(render(clockMs));
        const auto retained = pixels;
        fullRedraw = true;
        checkBounds(render(clockMs));
        assert(retained == pixels);
    }
    int frames = 0;
    for (int mood = 0; mood < kStateCount; ++mood) {
        resetVisual();
        state = -1;
        clockMs += 1000;
        enterState(mood, false);
        for (int i = 0; i < kStates[mood].nExpr; ++i)
            assert(kStates[mood].expr[i] < kExprCount);
        settledExpression(kStates[mood].expr[0]);
        for (int frame = 0; frame < 60; ++frame) {
            clockMs += 33;
            tick(.033f, clockMs);
            checkBounds(render(clockMs));
            if (!output.empty() && frame % 3 == 0) {
                saveFrame(output / ("motion-" + std::to_string(frames++) + ".ppm"));
                if (frame == 30)
                    saveFrame(output / ("mood-" + std::to_string(mood) + ".ppm"));
            }
        }
    }
    // Boundary poses, roll and a mid-blink expression change remain finite and bounded.
    resetVisual();
    state = SExcited;
    settledExpression(0);
    changeExpr(17, 12, clockMs);
    assert(pendingExpr == 17);
    changeExpr(0, 9, clockMs);
    assert(pendingExpr == -1); // A newer mood cancels the previous blink's pending eyes.
    startSpin();
    for (int frame = 0; frame < 90; ++frame) {
        clockMs += 33;
        dragX.t = frame < 45 ? -140 : 140;
        dragY.t = frame < 45 ? -120 : 110;
        if (frame == 15)
            changeExpr(17, 12, clockMs);
        tick(.033f, clockMs);
        checkBounds(render(clockMs));
    }
    assert(exprIndex >= 0 && exprIndex < kExprCount && std::isfinite(spin.x));
    puts("Original robot: 25 expressions, 12 animated moods, retained redraws and framebuffer "
         "guards passed.");
}
