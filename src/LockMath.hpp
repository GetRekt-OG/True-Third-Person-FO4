#pragma once
#include "CombatMath.hpp"
#include <string_view>
#include <cstdint>
namespace LockMath {
    // Device-local, packed device-local and combined Fallout mouse IDs.
    // Device check belongs here so keyboard/gamepad aliases cannot be swallowed.
    constexpr int MouseControl(bool mouse, std::uint32_t code, std::string_view name) {
        if (!mouse)
            return -1;
        switch (code) {
        case 2:
        case 0x10002:
            return 2;
        case 8:
        case 0x800:
        case 0x10800:
            return 8;
        case 9:
        case 0x900:
        case 0x10900:
            return 9;
        default:
            break;
        }
        if (name == "Zoom In" || name == "ZoomIn")
            return 8;
        if (name == "Zoom Out" || name == "ZoomOut")
            return 9;
        return -1;
    }
    constexpr bool Melee(int type) {
        return type >= 0 && type <= 6;
    }
    inline float SelectionScore(bool nearest, float yaw, float anchor, int direction, float dx,
                                float dy, float dz) {
        return nearest ? std::hypot(dx, dy, dz) : CombatMath::Score(yaw, anchor, direction);
    }
    inline std::array<float, 2> CameraCorrection(float dx, float dy, float dz, float fx, float fy,
                                                 float fz, float response, float dt) {
        if (!std::isfinite(dx + dy + dz + fx + fy + fz + dt) || std::hypot(dx, dy) < 0.01F ||
            std::hypot(fx, fy) < 0.01F)
            return {0, 0};
        const float alpha = 1 - std::exp(-response * std::clamp(dt, 0.0F, 0.05F));
        const float yaw = Logic::SignedYaw(std::atan2(dx, dy) - std::atan2(fx, fy));
        const float pitch =
            -std::atan2(dz, std::hypot(dx, dy)) + std::atan2(fz, std::hypot(fx, fy));
        // A replacement target can be behind the player. Bound the per-frame
        // turn rather than applying a large fraction of a 180-degree error at once.
        const float maxStep = 3.0F * std::clamp(dt, 0.0F, 0.05F);
        return {std::clamp(yaw * alpha, -maxStep, maxStep),
                std::clamp(pitch * alpha, -maxStep, maxStep)};
    }
} // namespace LockMath
