#pragma once
#include <cmath>

namespace IdleRotation {
    // Require no movement request AND no native movement output. Do not suppress
    // tiny walking input, deceleration, or controller drift while still moving.
    inline bool Stationary(float x, float y, float speed) {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(speed) &&
               std::hypot(x, y) <= 0.001F && std::abs(speed) <= 0.001F;
    }
    template <class Output> bool ClearCameraTurn(Output &output, float actorYaw) {
        if (!std::isfinite(actorYaw))
            return false;
        output.rotationSpeed.z = 0.0F;
        output.targetAngle.z = actorYaw;
        return true;
    }
} // namespace IdleRotation
