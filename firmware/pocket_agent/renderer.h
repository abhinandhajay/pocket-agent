#pragma once

// ---------- rendering ----------
static inline uint16_t pack(float r, float g, float b) {
    uint16_t c = (uint16_t(clampf(r, 0, 255)) >> 3) << 11 |
                 (uint16_t(clampf(g, 0, 255)) >> 2) << 5 | (uint16_t(clampf(b, 0, 255)) >> 3);
    return uint16_t((c >> 8) | (c << 8)); // sprite buffer is byte-swapped RGB565
}
static inline uint16_t unswap(uint16_t c) { return uint16_t((c >> 8) | (c << 8)); }

// Bilinear sample of a head SDF texture at head-unit coordinates.
static inline float sampleSdf(const int8_t *tex, float u, float v) {
    constexpr float inv = 1.0f / kSdfStep;
    float fx = (u - kSdfLo) * inv, fy = (v - kSdfLo) * inv;
    if (fx < 0 || fy < 0 || fx >= kSdfN - 1 || fy >= kSdfN - 1)
        return 31.75f;
    const int ix = int(fx), iy = int(fy);
    const float tx = fx - ix, ty = fy - iy;
    const int8_t *p = tex + iy * kSdfN + ix;
    const float a = p[0] + (p[1] - p[0]) * tx, b = p[kSdfN] + (p[kSdfN + 1] - p[kSdfN]) * tx;
    return (a + (b - a) * ty) / kSdfQuant;
}

static Rect prevRect = {0, 0, W, H};
static bool fullRedraw = true;
static constexpr Rect kBand = {100, 372, 368, 450};
static constexpr Rect kBandText = {100, 372, 368, 429};
static constexpr Rect kBandDots = {100, 429, 368, 450};
static Rect bandDirty{};
static bool legendValid = false;
static bool bandOn = false, previousBandOn = false;
static uint32_t profFill = 0, profTiles = 0, profExtra = 0, profShaded = 0, profPoly = 0;

static void drawBand(uint32_t now, RGB bg, Rect dirty) {
    const Assistant &p = assistant;
    const RGB l = rgb(kLabel);
    static char previousName[25]{}, previousDetail[44]{};
    char detail[44];
    const char *voiceText = badgevoice::activity(p.id);
    snprintf(detail, sizeof(detail), "%.43s",
             voiceText     ? voiceText
             : p.detail[0] ? p.detail
                           : kAssistantStateNames[int(p.state)]);
    const bool erased = dirty.x0 < kBandText.x1 && dirty.x1 > kBandText.x0 &&
                        dirty.y0 < kBandText.y1 && dirty.y1 > kBandText.y0;
    if (!legendValid || erased || strcmp(previousName, p.name) || strcmp(previousDetail, detail)) {
        canvas.fillRect(kBandText.x0, kBandText.y0, kBandText.x1 - kBandText.x0,
                        kBandText.y1 - kBandText.y0, unswap(pack(bg.r, bg.g, bg.b)));
        strcpy(previousName, p.name);
        strcpy(previousDetail, detail);
        legendValid = true;
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextSize(1);
        canvas.setFont(&fonts::FreeSansBold12pt7b);
        canvas.setTextColor(unswap(pack(l.r, l.g, l.b)));
        char name[28];
        snprintf(name, sizeof(name), "%.24s", p.name);
        while (canvas.textWidth(name) > 248 && strlen(name) > 4)
            strcpy(name + strlen(name) - 4, "...");
        canvas.drawString(name, W / 2, 390);
        canvas.setFont(&fonts::FreeSans9pt7b);
        while (canvas.textWidth(detail) > 236 && strlen(detail) > 4)
            strcpy(detail + strlen(detail) - 4, "...");
        canvas.setTextColor(
            unswap(pack(mixf(bg.r, l.r, 0.7f), mixf(bg.g, l.g, 0.7f), mixf(bg.b, l.b, 0.7f))));
        canvas.drawString(detail, W / 2, 417);
        bandDirty = kBand;
    } else
        bandDirty = kBandDots;
    canvas.fillRect(kBandDots.x0, kBandDots.y0, kBandDots.x1 - kBandDots.x0,
                    kBandDots.y1 - kBandDots.y0, unswap(pack(bg.r, bg.g, bg.b)));
    const uint16_t bg565ForBand = pack(bg.r, bg.g, bg.b);
    const float pulse = 0.5f + 0.5f * sinf(now / 1000.0f * 2 * PI * 1.2f);
    const float x = W / 2, y = 438, rf = 5, ro = rf + 2.8f;
    RGB c = rgb(kStyles[p.style % kStyleCount].ink);
    if (p.state == AssistantState::Sleeping || manualSleep)
        c = {c.r * .4f, c.g * .4f, c.b * .4f};
    int ring = -1;
    bool pulsing = false;
    if (p.state == AssistantState::Error) {
        ring = 0xFF4D4F;
        pulsing = p.unseen;
    } else if (p.state == AssistantState::Tool)
        ring = 0xF5B13F;
    else if (p.state == AssistantState::Thinking)
        ring = 0xD8D4CA;
    else if (p.unseen) {
        ring = 0x1D9BF0;
        pulsing = true;
    }
    if (ring >= 0) {
        const RGB rc = rgb(uint32_t(ring));
        const float a = pulsing ? .35f + .65f * pulse : 1;
        canvas.fillSmoothCircle(
            x, y, ro, unswap(pack(mixf(bg.r, rc.r, a), mixf(bg.g, rc.g, a), mixf(bg.b, rc.b, a))));
        canvas.fillSmoothCircle(x, y, rf + 1.2f, unswap(bg565ForBand));
    }
    canvas.fillSmoothCircle(x, y, rf, unswap(pack(c.r, c.g, c.b)));
}

static size_t replyLine(size_t start, char *out) {
    const char *text = badgereply::rx.text;
    size_t end = start, lastSpace = start;
    while (text[end] && text[end] != '\n' && end - start < 95) {
        out[end - start] = text[end];
        out[end - start + 1] = 0;
        if (canvas.textWidth(out) > 312) {
            if (lastSpace > start)
                end = lastSpace;
            break;
        }
        if (text[end] == ' ')
            lastSpace = end;
        end++;
    }
    memcpy(out, text + start, end - start);
    out[end - start] = 0;
    size_t next = end;
    if (next == start && text[next])
        next++;
    if (text[next] == '\n')
        next++;
    else
        while (text[next] == ' ')
            next++;
    return next;
}
static Rect renderReply() {
    bandDirty = {};
    static uint32_t previous = UINT32_MAX;
    static char previousReply[17]{};
    if (previous == badgereply::uiRevision && !strcmp(previousReply, badgereply::rid))
        return {0, 0, 0, 0};
    const bool fresh = strcmp(previousReply, badgereply::rid) != 0;
    strcpy(previousReply, badgereply::rid);
    previous = badgereply::uiRevision;
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextSize(1);
    if (fresh) {
        size_t pos = 0, count = 0;
        char line[97];
        badgereply::pages = 0;
        do {
            if (count % 12 == 0 && badgereply::pages < 349)
                badgereply::pageOffsets[badgereply::pages++] = uint16_t(pos);
            pos = replyLine(pos, line);
            count++;
        } while (badgereply::rx.text[pos] && count < 4097);
        if (!badgereply::pages)
            badgereply::pages = 1;
    }
    if (badgereply::page >= badgereply::pages)
        badgereply::page = badgereply::pages - 1;
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(textdatum_t::middle_center);
    char title[48];
    snprintf(title, sizeof(title), "Hermes reply  %u/%u", unsigned(badgereply::page + 1),
             unsigned(badgereply::pages));
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString(title, W / 2, 65);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(0x04FF);
    canvas.drawString(badgereply::rx.receiving ? "Receiving reply..." : "Text reply", W / 2, 94);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(textdatum_t::top_left);
    size_t pos = badgereply::pageOffsets[badgereply::page];
    char line[97];
    for (int row = 0; row < 12 && badgereply::rx.text[pos]; row++) {
        pos = replyLine(pos, line);
        canvas.drawString(line, 78, 118 + row * 20);
    }
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(0xBDF7);
    canvas.drawString("A/B or swipe: read   Both: close", W / 2, 396);
    canvas.drawString("Hold blue to talk", W / 2, 420);
    return {0, 0, W, H};
}
static void replyPage(int direction) {
    badgereply::page =
        uint16_t((int(badgereply::page) + direction + badgereply::pages) % badgereply::pages);
    badgereply::uiRevision++;
}
static void replyTouch() {
    static bool down = false;
    static float firstY = 0, lastY = 0;
    static uint32_t at = 0;
    lgfx::touch_point_t raw;
    if (!touchMapVersion)
        return;
    if (M5.Display.getTouchRaw(&raw)) {
        const auto point = touchcal::applyWarp(touchMap, {double(raw.x), double(raw.y)});
        lastY = point.y;
        if (!down) {
            down = true;
            firstY = lastY;
            at = millis();
        }
    } else if (down) {
        down = false;
        if (fabsf(lastY - firstY) > 35)
            replyPage(lastY < firstY ? 1 : -1);
        else if (millis() - at < 350 && lastY > 380) {
            badgereply::cancel();
            fullRedraw = true;
        }
    }
}
// Render inputs are finite, so these comparisons avoid library calls in pixel loops.
static inline float robotMin(float a, float b) { return a < b ? a : b; }
static inline float robotMax(float a, float b) { return a > b ? a : b; }

// Original Pocket robot. All artwork remains on the same rigid shell in every mood.
static float robotBox(float x, float y, float a, float b, float radius) {
    const float dx = fabsf(x) - a + radius, dy = fabsf(y) - b + radius;
    const float ox = robotMax(dx, 0), oy = robotMax(dy, 0);
    return fsqrt(ox * ox + oy * oy) + robotMin(robotMax(dx, dy), 0) - radius;
}
static inline float robotCircle(float x, float y, float radius) {
    // A cheap conservative bound avoids square roots far from a small detail.
    const float bound = robotMax(fabsf(x), fabsf(y)) - radius;
    return bound > 8 ? bound : fsqrt(x * x + y * y) - radius;
}

// Keep the MCU pixel loops inlined without changing floating-point semantics.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("O3")
#endif
static Rect render(uint32_t now) {
    bandDirty = {};
    const RGB bg = rgb(kBg), shell = {inkR.x, inkG.x, inkB.x};
    const RGB visor = rgb(kRobotVisor), eyes = rgb(kRobotEyes);
    const bool full = fullRedraw, labelOn = !assistant.used && now < labelUntil;
    const float seconds = now / 1000.0f;
    float hop = 0;
    if (bounceStart >= 0) {
        const float age = (now - uint32_t(bounceStart)) / 1000.0f;
        if (age >= .8f)
            bounceStart = -1;
        else
            hop = -32 * fabsf(sinf(age * PI * 2.5f)) * expf(-age * 3);
    }
    const float cx = kHomeX + o.x + dragX.x, cy = kHomeY + A.x + dragY.x + hop;
    const float angle = d.x * PI / 180 + spin.x;
    const float cs = cosf(angle), sn = sinf(angle);
    const float sx = kScale * clampf(sqx.x, .65f, 1.4f);
    const float sy = kScale * clampf(h.x * sqy.x, .65f, 1.4f);
    const float pixelScale = robotMin(sx, sy), invSx = 1 / sx, invSy = 1 / sy;
    struct Eye {
        Cap cap;
        float open, size, invOpen, invSize, cs, sn, bx, by;
    } eye[2];
    const bool settled = blend.x >= .999f;
    if (settled)
        loadEyeTex(exprIndex);
    for (int i = 0; i < 2; ++i) {
        eye[i].cap = blendedCap(i);
        eye[i].open = clampf(lid.x, .07f, 1.1f);
        if (i == winkEye && winkAt && uint32_t(now - winkAt) < 280)
            eye[i].open *= 1 - .91f * sinf((now - winkAt) / 280.0f * PI);
        eye[i].size = clampf(eyeScale.x, .8f, 1.12f);
        eye[i].invOpen = 1 / eye[i].open;
        eye[i].invSize = 1 / eye[i].size;
        eye[i].cs = cosf(eye[i].cap.ang);
        eye[i].sn = sinf(eye[i].cap.ang);
        eye[i].bx = eye[i].by = eye[i].cap.len + eye[i].cap.wid;
        if (settled) {
            eye[i].bx = eye[i].by = 0;
            for (int k = 0; k < kEyePts; ++k) {
                eye[i].bx =
                    robotMax(eye[i].bx, fabsf(kEyeContour[exprIndex][i][k][0] - eye[i].cap.cx));
                eye[i].by =
                    robotMax(eye[i].by, fabsf(kEyeContour[exprIndex][i][k][1] - eye[i].cap.cy));
            }
        }
    }
    auto eyeDistance = [&](int i, float x, float y) {
        const Eye &e = eye[i];
        const float qx = (x - (e.cap.cx - C) - gazeX.x * .65f) * e.invSize;
        const float qy = (y - (e.cap.cy - C) - gazeY.x * .5f) * e.invSize;
        const float outside = robotMax(fabsf(qx) - e.bx, fabsf(qy) - e.by * e.open);
        if (outside > 2)
            return outside * e.size;
        if (settled) {
            ++profPoly;
            // A missing gutter sample stays outside after the blink squash, while
            // remaining a conservative distance for tile classification.
            return sampleEyeTex(i, qx, qy * e.invOpen, e.invOpen * e.invSize) * e.open * e.size;
        }
        const float yy = qy * e.invOpen;
        const float along = qx * e.sn + yy * e.cs;
        const float across = qx * e.cs - yy * e.sn;
        const float end = robotMax(fabsf(along) - e.cap.len, 0);
        return (fsqrt(across * across + end * end) - e.cap.wid) * e.open * e.size;
    };
    RGB signal = rgb(state == SBlocked ? 0xFF7D79 : state == SWorking ? kRobotShell : kRobotEyes);
    float lamp[3] = {.3f, .3f, .3f};
    for (int i = 0; i < 3; ++i) {
        if (state == SThinking)
            lamp[i] = .25f + .75f * powf(.5f + .5f * sinf(seconds * 4 - i * 1.4f), 3);
        else if (state == SWorking)
            lamp[i] = int(seconds * 5) % 3 == i ? 1 : .2f;
        else if (state == SBlocked)
            lamp[i] = .45f + .55f * (.5f + .5f * sinf(seconds * 6));
        else if (state == SDone || state == SHappy || state == SExcited)
            lamp[i] = 1;
        else if (state == SSleeping)
            lamp[i] = .12f;
    }
    auto fields = [&](float x, float y, float *distance, unsigned mask = 127) {
        if (mask & 1)
            distance[0] = sampleSdf(sdfCur, x + C, y + C);
        if (mask & 2)
            distance[1] = robotBox(x, y - kRobotVisorY, kRobotVisorWidth, kRobotVisorHeight,
                                   kRobotVisorRadius);
        if (mask & 4)
            distance[2] = robotMin(eyeDistance(0, x, y), eyeDistance(1, x, y));
        if (mask & 8)
            distance[3] = robotCircle(x + 13, y + 102, 7.5f);
        for (int i = 0; i < 3; ++i)
            if (mask & (16U << i))
                distance[4 + i] = robotCircle(x - (i - 1) * 13, y - 39, 2.5f);
    };
    auto inkAt = [&](const float *distance) {
        const float body = clampf(.5f - distance[0] * pixelScale, 0, 1);
        if (body <= 0)
            return bg;
        const float panel = clampf(.5f - distance[1] * pixelScale, 0, 1);
        RGB c = shell;
        auto over = [&](RGB ink, float alpha) {
            if (alpha >= 1)
                c = ink;
            else if (alpha > 0)
                c = {mixf(c.r, ink.r, alpha), mixf(c.g, ink.g, alpha), mixf(c.b, ink.b, alpha)};
        };
        if (panel > 0) {
            over(visor, panel);
            over(eyes, clampf(.5f - distance[2] * pixelScale, 0, 1) * panel);
            for (int i = 0; i < 3; ++i)
                over({signal.r * lamp[i], signal.g * lamp[i], signal.b * lamp[i]},
                     clampf(.5f - distance[4 + i] * pixelScale, 0, 1) * panel);
        }
        over(signal, clampf(.5f - distance[3] * pixelScale, 0, 1));
        if (body < 1)
            c = {mixf(bg.r, c.r, body), mixf(bg.g, c.g, body), mixf(bg.b, c.b, body)};
        return c;
    };
    auto color = [&](const float *distance) {
        const RGB c = inkAt(distance);
        return pack(c.r, c.g, c.b);
    };
    struct PaletteInk {
        RGB rgb;
        uint16_t packed;
    };
    static PaletteInk palette[128];
    bool paletteValid[128]{};
    auto paletteAt = [&](unsigned mask) -> const PaletteInk & {
        if (!paletteValid[mask]) {
            float distance[7];
            for (int i = 0; i < 7; ++i)
                distance[i] = mask & (1U << i) ? -1e6f : 1e6f;
            const RGB c = inkAt(distance);
            palette[mask] = {c, pack(c.r, c.g, c.b)};
            paletteValid[mask] = true;
        }
        return palette[mask];
    };
    // Shell, ears and antenna fit inside this radius at every rotation.
    const float reach = 116 * robotMax(sx, sy) + 2;
    Rect current = {int(cx - reach), int(cy - reach), int(cx + reach) + 1, int(cy + reach) + 1};
    if (state == SSleeping) {
        current.x1 = max(current.x1, int(cx + 136));
        current.y0 = min(current.y0, int(cy - 136));
    }
    for (int i = 0; i < partCount; ++i) {
        const Particle &p = parts[i];
        const int radius = int(p.size) + 2;
        current.x0 = min(current.x0, int(p.x) - radius);
        current.x1 = max(current.x1, int(p.x) + radius);
        current.y0 = min(current.y0, int(p.y) - radius);
        current.y1 = max(current.y1, int(p.y) + radius);
    }
    current = {max(0, current.x0), max(0, current.y0), min(W, current.x1), min(H, current.y1)};
    Rect dirty = full ? Rect{0, 0, W, H}
                      : Rect{min(current.x0, prevRect.x0), min(current.y0, prevRect.y0),
                             max(current.x1, prevRect.x1), max(current.y1, prevRect.y1)};
    uint32_t at = micros();
    canvas.fillRect(dirty.x0, dirty.y0, dirty.x1 - dirty.x0, dirty.y1 - dirty.y0,
                    unswap(pack(bg.r, bg.g, bg.b)));
    profFill += micros() - at;
    at = micros();
    auto sample = [&](float x, float y, float *distance, unsigned mask = 127) {
        const float dx = x - cx, dy = y - cy;
        fields((dx * cs + dy * sn) * invSx, (-dx * sn + dy * cs) * invSy, distance, mask);
    };
    auto tile = [&](int x0, int y0, int x1, int y1, bool leaf) {
        float distance[7];
        sample((x0 + x1) * .5f, (y0 + y1) * .5f, distance, 1U);
        const float radius = (leaf ? 2.828428f : 5.656855f) + 2;
        if (distance[0] * pixelScale > radius)
            return true;
        sample((x0 + x1) * .5f, (y0 + y1) * .5f, distance, 126U);
        unsigned mask = 0, insideMask = 0;
        for (int i = 0; i < 7; ++i) {
            if (fabsf(distance[i] * pixelScale) <= radius)
                mask |= 1U << i;
            if (distance[i] < 0)
                insideMask |= 1U << i;
        }
        // Only edge fields vary within a tile. The visor hides the shell and antenna,
        // while eyes and mouth contribute only inside the visor.
        if (distance[1] * pixelScale < -radius)
            mask &= ~(1U | 8U);
        else if (distance[1] * pixelScale > radius)
            mask &= ~(4U | 16U | 32U | 64U);
        const bool flat = mask == 0;
        if (!flat && !leaf)
            return false;
        const uint16_t fill = flat ? paletteAt(insideMask).packed : 0;
        const bool singleEdge = mask && !(mask & (mask - 1));
        const int edge = singleEdge ? __builtin_ctz(mask) : 0;
        RGB outside{}, inside{};
        uint16_t outside565 = 0, inside565 = 0;
        // A tile with one edge blends two fixed inks; only its antialiased pixels
        // need color arithmetic. Interior pixels use prepacked RGB565.
        if (singleEdge) {
            const auto &low = paletteAt(insideMask & ~mask);
            const auto &high = paletteAt(insideMask | mask);
            outside = low.rgb;
            outside565 = low.packed;
            inside = high.rgb;
            inside565 = high.packed;
        }
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                if (!flat) {
                    sample(x + .5f, y + .5f, distance, mask);
                    ++profShaded;
                }
                if (flat)
                    fb[y * W + x] = fill;
                else if (singleEdge) {
                    const float alpha = clampf(.5f - distance[edge] * pixelScale, 0, 1);
                    fb[y * W + x] = alpha <= 0   ? outside565
                                    : alpha >= 1 ? inside565
                                                 : pack(mixf(outside.r, inside.r, alpha),
                                                        mixf(outside.g, inside.g, alpha),
                                                        mixf(outside.b, inside.b, alpha));
                } else
                    fb[y * W + x] = color(distance);
            }
        return true;
    };
    for (int y = current.y0; y < current.y1; y += 8)
        for (int x = current.x0; x < current.x1; x += 8) {
            const int x1 = min(x + 8, current.x1), y1 = min(y + 8, current.y1);
            if (!tile(x, y, x1, y1, false))
                for (int yy = y; yy < y1; yy += 4)
                    for (int xx = x; xx < x1; xx += 4)
                        tile(xx, yy, min(xx + 4, x1), min(yy + 4, y1), true);
        }
    profTiles += micros() - at;
    at = micros();
    for (int i = 0; i < partCount; ++i) {
        const Particle &p = parts[i];
        const RGB c = rgb(p.color);
        const float alpha = clampf(1.8f - p.life, 0, 1);
        canvas.fillSmoothCircle(p.x, p.y, p.size * .35f,
                                unswap(pack(c.r * alpha, c.g * alpha, c.b * alpha)));
    }
    if (badge)
        canvas.fillSmoothCircle(cx + 87 * sx, cy - 66 * sy, 6, unswap(pack(92, 167, 255)));
    bandOn = assistant.used || labelOn || previousBandOn;
    if (assistant.used)
        drawBand(now, bg, dirty);
    else {
        legendValid = false;
        if (bandOn) {
            canvas.fillRect(kBand.x0, kBand.y0, kBand.x1 - kBand.x0, kBand.y1 - kBand.y0, 0);
            bandDirty = kBand;
        }
    }
    previousBandOn = assistant.used || labelOn;
    if (labelOn) {
        canvas.setFont(&fonts::FreeSansBold12pt7b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextSize(1);
        canvas.setTextColor(unswap(pack(185, 180, 168)));
        canvas.drawString(labelText, W / 2, 400);
    }
    if (state == SSleeping) {
        canvas.setFont(&fonts::FreeSansBold12pt7b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(unswap(pack(120, 170, 180)));
        canvas.setTextSize(.8f);
        canvas.drawString("z", int(cx + 112), int(cy - 99 - 8 * sinf(seconds * 1.5f)));
        canvas.setTextSize(1);
    }
    prevRect = current;
    fullRedraw = false;
    // Restore the retained Settings gear if this frame erased any of it.
    if (full || (dirty.x0 < 260 && dirty.x1 > 208 && dirty.y0 < 82 && dirty.y1 > 30)) {
        canvas.fillCircle(234, 56, 19, 0);
        static constexpr int8_t outline[][2] = {
            {-3, -15}, {3, -15}, {4, -11}, {7, -9},   {11, -10}, {14, -5},   {11, -2}, {11, 2},
            {14, 5},   {11, 10}, {7, 9},   {4, 11},   {3, 15},   {-3, 15},   {-4, 11}, {-7, 9},
            {-11, 10}, {-14, 5}, {-11, 2}, {-11, -2}, {-14, -5}, {-11, -10}, {-7, -9}, {-4, -11},
        };
        constexpr uint32_t ink = 0xA4ABB4U;
        constexpr int count = sizeof(outline) / sizeof(outline[0]);
        for (int i = 0; i < count; ++i) {
            const auto &a = outline[i], &b = outline[(i + 1) % count];
            canvas.drawWideLine(234 + a[0], 56 + a[1], 234 + b[0], 56 + b[1], 1.0f, ink);
        }
        canvas.fillSmoothCircle(234, 56, 5, ink);
        canvas.fillSmoothCircle(234, 56, 3, 0);
        dirty = {min(dirty.x0, 208), min(dirty.y0, 30), max(dirty.x1, 260), max(dirty.y1, 82)};
    }
    profExtra += micros() - at;
    return dirty;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
