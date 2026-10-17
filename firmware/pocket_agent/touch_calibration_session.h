#pragma once
#include "touch_calibration_core.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Shared guided workflow. Active calibration is owned by the caller until commit.
namespace touchcal {
enum class Phase { Off, Cardinal, Clockwise, Counterclockwise, ShooterIntro, Shooter, Review };
struct GuidedSession {
    Phase phase = Phase::Off;
    WarpMap candidate{};
    Point cardinal[4]{}, samples[128]{}, start{};
    Residual perimeter[2][16]{}, centers[3]{}, shooter[3][16]{};
    double sumX[2][16]{}, sumY[2][16]{};
    unsigned counts[2][16]{};
    bool retry[51]{};
    unsigned index = 0, sampleCount = 0;
    uint32_t endpointAt = 0;
    bool down = false, moved = false, ready = false;
    void begin() {
        *this = GuidedSession{};
        phase = Phase::Cardinal;
    }
    void cancel() {
        phase = Phase::Off;
        down = false;
    }
    bool active() const { return phase != Phase::Off; }
    static bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
    void append(Point p) {
        if (sampleCount == 128) {
            std::memmove(samples, samples + 1, 127 * sizeof(Point));
            sampleCount--;
        }
        samples[sampleCount++] = p;
    }
    bool covered(unsigned pass) const {
        for (unsigned s = 0; s < 16; ++s)
            if (counts[pass][s] < 4)
                return false;
        return true;
    }
    void construct() {
        Point centerPoints[3];
        for (unsigned i = 0; i < 3; ++i)
            centerPoints[i] = {centers[i].dx, centers[i].dy};
        const auto c = reduceAttempt(centerPoints, 3).median;
        candidate.center = {c.x, c.y};
        for (unsigned r = 0; r < 3; ++r)
            smoothRing(shooter[r], candidate.rings[r]);
        Residual combined[16], smoothPerimeter[16], outer[16];
        averageRings(perimeter[0], perimeter[1], combined);
        smoothRing(combined, smoothPerimeter);
        averageRings(candidate.rings[2], smoothPerimeter, outer);
        smoothRing(outer, candidate.rings[2]);
        ready = validWarp(candidate);
        phase = Phase::Review;
    }
    void acceptShot(Point raw) {
        AsteroidTarget target{};
        asteroidTarget(index, &target);
        const auto mapped = apply(candidate.affine, raw);
        const Residual residual{target.center.x - mapped.x, target.center.y - mapped.y};
        const unsigned id = (index * 20 + 7) % 51;
        if (!target.radialLayer)
            centers[id] = residual;
        else
            shooter[target.radialLayer - 1][target.sector] = residual;
        retry[index] = false;
        do {
            ++index;
        } while (index < 51 && retrying && !retry[index]);
        if (index == 51)
            construct();
    }
    bool retrying = false;
    void mark(unsigned ring, unsigned sector) {
        for (unsigned a = 0; a < 51; ++a) {
            AsteroidTarget t{};
            asteroidTarget(a, &t);
            if (t.radialLayer == ring && (ring == 0 || t.sector == sector))
                retry[a] = true;
        }
        if (ring == 3) {
            for (unsigned p = 0; p < 2; ++p) {
                counts[p][sector] = 0;
                sumX[p][sector] = sumY[p][sector] = 0;
            }
        }
    }
    void retryUnsafe() {
        std::memset(retry, 0, sizeof(retry));
        auto length = [](Residual r) { return std::hypot(r.dx, r.dy); };
        auto delta = [&](Residual a, Residual b) { return length({a.dx - b.dx, a.dy - b.dy}); };
        if (length(candidate.center) > kMaximumResidualMagnitude)
            mark(0, 0);
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned s = 0; s < 16; ++s) {
                const auto v = candidate.rings[r][s];
                if (length(v) > kMaximumResidualMagnitude)
                    mark(r + 1, s);
                if (delta(v, candidate.rings[r][(s + 1) % 16]) > kMaximumNeighborDelta) {
                    mark(r + 1, s);
                    mark(r + 1, (s + 1) % 16);
                }
                const auto inner = r ? candidate.rings[r - 1][s] : candidate.center;
                if (delta(v, inner) > kMaximumNeighborDelta) {
                    mark(r + 1, s);
                    mark(r, s);
                }
            }
        index = 0;
        while (index < 51 && !retry[index])
            ++index;
        if (index == 51) {
            begin();
            return;
        }
        retrying = true;
        phase = covered(0) ? Phase::ShooterIntro : Phase::Clockwise;
    }
    void enter() {
        if (phase == Phase::ShooterIntro) {
            phase = Phase::Shooter;
            down = false;
            sampleCount = 0;
        } else if (phase == Phase::Review && !ready)
            retryUnsafe();
    }
    void undo() {
        if (phase == Phase::Shooter && index && !retrying)
            --index;
    }
    void feed(bool pressed, Point raw, uint32_t now) {
        if (!active() || (pressed && !finite(raw)))
            return;
        if (pressed && !down) {
            start = raw;
            sampleCount = 0;
            moved = false;
            endpointAt = now;
        }
        if (phase == Phase::Cardinal && pressed) {
            if (!moved && std::hypot(raw.x - start.x, raw.y - start.y) >= 80) {
                moved = true;
                endpointAt = now;
                sampleCount = 0;
            }
            if (moved) {
                append(raw);
                if (sampleCount >= kMinimumSamples) {
                    const auto result = reduceSamples(samples, sampleCount, now - endpointAt);
                    if (result.accepted) {
                        cardinal[index++] = result.median;
                        sampleCount = 0;
                        moved = false;
                        if (index == 4) {
                            if (fitAffine(cardinal, kCardinalTargets, 4, &candidate.affine)) {
                                phase = Phase::Clockwise;
                                index = 0;
                            } else {
                                index = 0;
                            }
                        }
                        // Consume contact until release; it cannot capture the next destination.
                        waitingRelease = true;
                    } else if (now - endpointAt > 600) {
                        sampleCount = 0;
                        endpointAt = now;
                    }
                }
            }
        } else if ((phase == Phase::Clockwise || phase == Phase::Counterclockwise) && pressed) {
            const unsigned pass = phase == Phase::Clockwise ? 0 : 1;
            const auto p = apply(candidate.affine, raw);
            const unsigned s = perimeterSector(p);
            const double dx = p.x - kCenter.x, dy = p.y - kCenter.y, radius = std::hypot(dx, dy);
            if (s < 16 && radius > 1 && (!retrying || counts[pass][s] < 4)) {
                sumX[pass][s] += dx * 170 / radius - dx;
                sumY[pass][s] += dy * 170 / radius - dy;
                ++counts[pass][s];
            }
            if (covered(pass)) {
                for (unsigned s2 = 0; s2 < 16; ++s2)
                    perimeter[pass][s2] = {sumX[pass][s2] / counts[pass][s2],
                                           sumY[pass][s2] / counts[pass][s2]};
                phase = pass == 0 ? Phase::Counterclockwise : Phase::ShooterIntro;
                waitingRelease = true;
            }
        } else if (phase == Phase::Shooter && pressed)
            append(raw);
        else if (phase == Phase::Shooter && !pressed && down) {
            const auto result = reduceAttempt(samples, sampleCount);
            if (result.accepted)
                acceptShot(result.median);
        } else if (phase == Phase::ShooterIntro && !pressed && down)
            enter();
        down = pressed;
    }
    bool waitingRelease = false;
    void touch(bool pressed, Point raw, uint32_t now) {
        if (waitingRelease) {
            if (!pressed) {
                waitingRelease = false;
                down = false;
            }
            return;
        }
        feed(pressed, raw, now);
    }
};
} // namespace touchcal
