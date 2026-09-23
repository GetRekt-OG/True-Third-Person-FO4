#pragma once
#include "FacingBlend.hpp"
namespace TrueThirdPerson::ADSTurn {
    inline bool pending = false;
    inline float yaw = 0, remaining = 0;
    inline void Cancel() {
        pending = false;
        remaining = 0;
    }
    inline void Begin(float angle, float duration) {
        yaw = angle;
        remaining = duration;
        pending = true;
    }
    inline bool Step(float target, float dt) {
        if (!pending)
            return false;
        dt = std::isfinite(dt) ? std::clamp(dt, 0.0F, 0.05F) : 0;
        yaw = FacingBlend::Step(yaw, target, dt, remaining);
        remaining = std::max(0.0F, remaining - dt);
        return remaining <= 0;
    }
} // namespace TrueThirdPerson::ADSTurn
