#pragma once
#include "Logic.hpp"
#include <algorithm>
namespace FacingBlend {
    inline float Step(float current, float target, float dt, float remaining = 0) {
        if (!std::isfinite(current) || !std::isfinite(target) || !std::isfinite(dt))
            return current;
        dt = std::clamp(dt, 0.0F, 0.05F);
        const float alpha = remaining > 0 ? std::min(dt / remaining, 1.0F) : 1 - std::exp(-24 * dt);
        return Logic::Normalize(current + Logic::SignedYaw(target - current) * alpha);
    }
} // namespace FacingBlend
