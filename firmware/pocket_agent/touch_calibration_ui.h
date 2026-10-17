#pragma once
#include "touch_calibration_session.h"
static touchcal::GuidedSession calibration;
static void calibrationEvidence() {
    netOut.printf("GB_CAL phase=%d index=%u stack_free=%u\n", int(calibration.phase),
                  calibration.index, uxTaskGetStackHighWaterMark(nullptr));
}
static void calibrationStart() {
    calibration.begin();
    calibrationEvidence();
}
static void calibrationCancel() {
    calibration.cancel();
    netOut.println("GB_CAL cancelled");
}
static bool calibrationCommit() {
    if (!calibration.ready || !touchcal::validWarp(calibration.candidate))
        return false;
    const auto record = touchcal::makeRecordV2(calibration.candidate, touchMapGeneration + 1);
    if (!touchcal::validateRecordV2(record))
        return false;
    Preferences prefs;
    if (!prefs.begin(touchcal::kNvsNamespace, false))
        return false;
    const bool saved = prefs.putBytes("record", &record, sizeof(record)) == sizeof(record);
    prefs.end();
    if (!saved) {
        netOut.println("GB_CAL save_failed");
        return false;
    }
    touchMap = calibration.candidate;
    touchMapVersion = 2;
    touchMapGeneration = record.generation;
    calibration.cancel();
    netOut.printf("GB_TOUCHMAP ok=1 version=2 generation=%u saved=1\n",
                  unsigned(touchMapGeneration));
    return true;
}
static void calibrationInput(badgeinput::Input input) {
    if (input == badgeinput::Input::Escape) {
        calibrationCancel();
        return;
    }
    if (input == badgeinput::Input::Prev)
        calibration.undo();
    if (input == badgeinput::Input::Enter) {
        if (calibration.phase == touchcal::Phase::Review && calibration.ready)
            calibrationCommit();
        else
            calibration.enter();
    }
}
static void calibrationTick(uint32_t now) {
    const auto phase = calibration.phase;
    const auto index = calibration.index;
    lgfx::touch_point_t raw{};
    const bool pressed = M5.Display.getTouchRaw(&raw) > 0;
    calibration.touch(pressed, {double(raw.x), double(raw.y)}, now);
    if (phase != calibration.phase || index != calibration.index)
        calibrationEvidence();
    static uint32_t observedAt = 0;
    if (pressed && now - observedAt >= 150) {
        observedAt = now;
        const auto affine =
            touchcal::apply(calibration.candidate.affine, {double(raw.x), double(raw.y)});
        const auto mapped = touchcal::applyWarp(touchMap, {double(raw.x), double(raw.y)});
        netOut.printf("GB_CAL_OBS phase=%d raw=%d,%d affine=%.1f,%.1f mapped=%.1f,%.1f index=%u\n",
                      int(calibration.phase), raw.x, raw.y, affine.x, affine.y, mapped.x, mapped.y,
                      calibration.index);
    }
}
static void calibrationDraw() {
    using touchcal::Phase;
    canvas.fillScreen(0);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(TFT_WHITE, 0);
    canvas.setFont(&fonts::Font2);
    const auto phase = calibration.phase;
    if (phase == Phase::Cardinal) {
        static const char *directions[] = {"UP", "DOWN", "LEFT", "RIGHT"};
        const auto destination = touchcal::kCardinalTargets[calibration.index];
        canvas.fillCircle(234, 233, 5, TFT_WHITE);
        canvas.fillCircle(destination.x, destination.y, 7, 0xFF7A00U);
        canvas.drawString("Drag from the center " + String(directions[calibration.index]) +
                              " to the orange dot",
                          234, 300);
    } else if (phase == Phase::Clockwise || phase == Phase::Counterclockwise) {
        const unsigned pass = phase == Phase::Clockwise ? 0 : 1;
        canvas.drawCircle(234, 233, 170, 0x444444U);
        canvas.drawString("AROUND THE WORLD", 234, 194);
        canvas.drawString(pass ? "COUNTERCLOCKWISE" : "CLOCKWISE", 234, 221);
        canvas.drawString("Lifts and imperfect radii are fine", 234, 249);
        for (unsigned s = 0; s < 16; ++s) {
            const double a = s * 6.283185307179586 / 16;
            canvas.fillCircle(234 + std::sin(a) * 170, 233 - std::cos(a) * 170, 5,
                              calibration.counts[pass][s] >= 4 ? TFT_GREEN : 0xFF7A00U);
        }
    } else if (phase == Phase::ShooterIntro) {
        canvas.drawString("ASTEROID SHOOTER", 234, 216);
        canvas.drawString("Tap to start", 234, 251);
    } else if (phase == Phase::Shooter) {
        touchcal::AsteroidTarget t{};
        touchcal::asteroidTarget(calibration.index, &t);
        const int x = t.center.x, y = t.center.y, r = t.visualRadius;
        if (t.shape == touchcal::TargetShape::FilledCircle)
            canvas.fillCircle(x, y, r, t.rgb);
        else if (t.shape == touchcal::TargetShape::Ring) {
            canvas.drawCircle(x, y, r, t.rgb);
            canvas.drawCircle(x, y, r - 1, t.rgb);
        } else {
            const int points = t.shape == touchcal::TargetShape::Hexagon ? 6 : 9;
            for (int i = 0; i < points; ++i) {
                const double a = i * 6.283185307179586 / points,
                             b = (i + 1) * 6.283185307179586 / points;
                const double ra = r * (points == 9 && i % 2 ? .72 : 1),
                             rb = r * (points == 9 && (i + 1) % 2 ? .72 : 1);
                canvas.fillTriangle(x, y, x + std::sin(a) * ra, y - std::cos(a) * ra,
                                    x + std::sin(b) * rb, y - std::cos(b) * rb, t.rgb);
            }
        }
        canvas.fillCircle(x, y, 2, TFT_WHITE);
    } else if (phase == Phase::Review) {
        canvas.drawString(calibration.ready ? "TOUCH MAP READY" : "RECOLLECT UNSAFE REGIONS", 234,
                          196);
        canvas.drawString(calibration.ready ? "Both briefly to save and apply"
                                            : "Both briefly to retry nearby targets",
                          234, 234);
        canvas.drawString("Hold both to cancel", 234, 267);
    }
    canvas.pushSprite(0, 0);
}
