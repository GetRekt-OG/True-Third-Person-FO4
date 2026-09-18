#pragma once
#include <cmath>
#include <numbers>
namespace Logic {
    inline float Normalize(float a) noexcept {
        const float result = std::fmod(a, 2.0F * std::numbers::pi_v<float>);
        return result < 0 ? result + 2.0F * std::numbers::pi_v<float> : result;
    }
    inline float SignedYaw(float a) noexcept {
        return Normalize(a + std::numbers::pi_v<float>) - std::numbers::pi_v<float>;
    }
} // namespace Logic
