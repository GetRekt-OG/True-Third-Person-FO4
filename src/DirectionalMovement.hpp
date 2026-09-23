#pragma once
#include "Logic.hpp"
#include <algorithm>
#include <cmath>

namespace DirectionalMovement {
    // Independent Weapon Facing 7.2 movement scheme; six radians per second.
    inline float TurnDelta(float x, float y, float cameraYaw, float actorYaw, float dt) noexcept {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(cameraYaw) ||
            !std::isfinite(actorYaw) || !std::isfinite(dt) || std::hypot(x, y) <= 0.10F)
            return 0.0F;
        const float delta = Logic::SignedYaw(cameraYaw + std::atan2(x, y) - actorYaw);
        const float step = 6.0F * std::clamp(dt, 0.0F, 0.05F);
        return std::clamp(delta, -step, step);
    }
} // namespace DirectionalMovement
