#include "touch_calibration_session.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    touchcal::GuidedSession session;
    session.begin();
    uint32_t now = 100;
    for (unsigned i = 0; i < 4; ++i) {
        session.touch(true, touchcal::kCenter, now++);
        for (unsigned n = 0; n < 20; ++n) {
            now += 10;
            session.touch(true, touchcal::kCardinalTargets[i], now);
        }
        session.touch(false, {}, now++);
    }
    assert(session.phase == touchcal::Phase::Clockwise);
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned sector = 0; sector < 16; ++sector) {
            const double angle = (pass ? 15 - sector : sector) * 2 * 3.141592653589793 / 16;
            for (unsigned n = 0; n < 4; ++n) {
                session.touch(true, {234 + std::sin(angle) * 170, 233 - std::cos(angle) * 170},
                              now++);
                session.touch(false, {}, now++);
            }
        }
    }
    assert(session.phase == touchcal::Phase::ShooterIntro);
    session.touch(true, {10, 10}, now++);
    session.touch(false, {}, now++);
    assert(session.phase == touchcal::Phase::Shooter && session.index == 0);
    for (unsigned i = 0; i < 51; ++i) {
        touchcal::AsteroidTarget target{};
        touchcal::asteroidTarget(i, &target);
        session.touch(true, target.center, now++);
        session.touch(false, {}, now++);
    }
    assert(session.phase == touchcal::Phase::Review && session.ready);
    assert(touchcal::validWarp(session.candidate));
    for (auto p : touchcal::kCardinalTargets) {
        const auto mapped = touchcal::applyWarp(session.candidate, p);
        assert(std::hypot(mapped.x - p.x, mapped.y - p.y) < 0.001);
    }
    // Recollect unsafe center/inner-ring nodes without discarding a completed middle ring.
    session.shooter[1][5] = {2, 1};
    for (auto &residual : session.centers)
        residual = {100, 0};
    session.construct();
    assert(!session.ready);
    session.enter();
    assert(session.retrying && session.phase == touchcal::Phase::ShooterIntro);
    session.enter();
    unsigned retried = 0;
    while (session.phase == touchcal::Phase::Shooter) {
        touchcal::AsteroidTarget target{};
        touchcal::asteroidTarget(session.index, &target);
        assert(target.radialLayer <= 1);
        session.touch(true, target.center, now++);
        session.touch(false, {}, now++);
        ++retried;
    }
    assert(retried == 19 && session.ready);
    assert(session.shooter[1][5].dx == 2 && session.shooter[1][5].dy == 1);
    session.cancel();
    assert(!session.active());
    session.begin();
    session.touch(true, touchcal::kCenter, now++);
    for (unsigned n = 0; n < 30; ++n)
        session.touch(true, touchcal::kCenter, now += 10);
    assert(session.index == 0);
    session.cancel();
    std::cout
        << "Guided calibration: cardinal, bidirectional perimeter, discarded start tap, 51 "
           "targets, valid map, partial unsafe-region retry, cancel and center rejection passed.\n";
}
