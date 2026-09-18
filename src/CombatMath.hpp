#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "Logic.hpp"
namespace CombatMath {
    inline std::array<float, 3> Forward(float w, float x, float y, float z) {
        return {-2 * w * z + 2 * x * y, 1 - 2 * x * x - 2 * z * z, 2 * w * x + 2 * y * z};
    }
    inline float Turn(float current, float desired, float response, float dt) {
        if (!std::isfinite(current) || !std::isfinite(desired) || !std::isfinite(dt))
            return current;
        const float alpha = 1 - std::exp(-response * std::clamp(dt, 0.0F, 0.05F));
        return current + Logic::SignedYaw(desired - current) * alpha;
    }
    inline float Score(float yaw, float anchor, int direction) {
        if (!std::isfinite(yaw) || !std::isfinite(anchor) || std::abs(yaw) > 1.40F)
            return std::numeric_limits<float>::infinity();
        const float side = yaw - anchor;
        if ((direction > 0 && side <= 0.02F) || (direction < 0 && side >= -0.02F))
            return std::numeric_limits<float>::infinity();
        return std::abs(direction ? side : yaw);
    }
} // namespace CombatMath
