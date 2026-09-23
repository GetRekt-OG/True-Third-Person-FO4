#pragma once
#include <cmath>
#include <cstdint>

namespace TrueThirdPerson::ControllerInput {
    constexpr bool LockButton(bool gamepad, std::uint32_t code) noexcept {
        return gamepad && (code == 0x80 || code == 0x10080);
    }
    constexpr bool LookStick(bool gamepad, std::uint32_t code) noexcept {
        return gamepad && (code == 12 || code == 0x1000C);
    }

    // A held stick never repeats. Require neutral even after acquiring a lock,
    // so the camera input used to find an enemy cannot immediately switch it.
    struct Flick {
        bool armed = false;
        void Reset() noexcept { armed = false; }
        int Update(float x, float y, bool enabled, bool cooldownReady) noexcept {
            if (!enabled || !std::isfinite(x) || !std::isfinite(y)) {
                Reset();
                return 0;
            }
            if (std::hypot(x, y) <= 0.25F) {
                armed = true;
                return 0;
            }
            if (std::abs(x) < 0.65F || std::abs(x) <= std::abs(y))
                return 0;
            const bool trigger = armed && cooldownReady;
            armed = false;
            return trigger ? (x > 0 ? 1 : -1) : 0;
        }
    };
}
