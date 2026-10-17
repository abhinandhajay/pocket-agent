#pragma once
#include <cstdint>
namespace badgevoice {
constexpr uint32_t kHoldMs = 600;
enum class Edge { None, Start, Stop, Cancel };
struct Gesture {
    Edge edge;
    bool consume;
};
class HoldButton {
  public:
    Gesture update(uint32_t now, bool a, bool b, bool enabled) {
        if (active_) {
            if (a || !enabled) {
                active_ = false;
                drain_ = true;
                return {Edge::Cancel, true};
            }
            if (!b) {
                active_ = false;
                drain_ = a || b;
                candidate_ = false;
                return {Edge::Stop, true};
            }
            return {Edge::None, true};
        }
        if (drain_) {
            if (!a && !b)
                drain_ = false;
            return {Edge::None, true};
        }
        if (chord_) {
            if (!a && !b)
                chord_ = false;
            return {Edge::None, false};
        }
        if (a) {
            chord_ = true;
            candidate_ = false;
            return {Edge::None, false};
        }
        if (a || !b || !enabled) {
            candidate_ = false;
            return {Edge::None, false};
        }
        if (!candidate_) {
            candidate_ = true;
            down_ = now;
        }
        if (uint32_t(now - down_) < kHoldMs)
            return {Edge::None, false};
        candidate_ = false;
        active_ = true;
        return {Edge::Start, true};
    }

  private:
    bool candidate_ = false, active_ = false, drain_ = false, chord_ = false;
    uint32_t down_ = 0;
};
} // namespace badgevoice
