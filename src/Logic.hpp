#pragma once
// Engine-free logic. Everything here builds and runs in tests/ without the game.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>

namespace TrueThirdPerson {
    namespace Logic {
        // A number from an INI value, or the fallback if it isn't one or is out of range.
        inline float ParseNumber(const wchar_t *text, float fallback, float minimum,
                                 float maximum) noexcept {
            wchar_t *end = nullptr;
            const float value = std::wcstof(text, &end);
            if (end == text || !std::isfinite(value))
                return fallback;
            while (std::iswspace(*end))
                ++end;
            return *end || value < minimum || value > maximum ? fallback : value;
        }
        inline float Normalize(float a) noexcept {
            const float result = std::fmod(a, 2.0F * std::numbers::pi_v<float>);
            return result < 0 ? result + 2.0F * std::numbers::pi_v<float> : result;
        }
        inline float SignedYaw(float a) noexcept {
            return Normalize(a + std::numbers::pi_v<float>) - std::numbers::pi_v<float>;
        }
    }

    namespace Facing {
        // Frame-rate independent turn toward target. With `remaining` > 0 the
        // turn finishes exactly when the remaining time runs out.
        inline float Blend(float current, float target, float dt, float remaining = 0) {
            if (!std::isfinite(current) || !std::isfinite(target) || !std::isfinite(dt))
                return current;
            dt = std::clamp(dt, 0.0F, 0.05F);
            const float alpha =
                remaining > 0 ? std::min(dt / remaining, 1.0F) : 1 - std::exp(-24 * dt);
            return Logic::Normalize(current + Logic::SignedYaw(target - current) * alpha);
        }

        // Movement turn toward the stick direction relative to the camera, 6 rad/s.
        inline float MoveTurn(float x, float y, float cameraYaw, float actorYaw, float dt) noexcept {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(cameraYaw) ||
                !std::isfinite(actorYaw) || !std::isfinite(dt) || std::hypot(x, y) <= 0.10F)
                return 0.0F;
            const float delta = Logic::SignedYaw(cameraYaw + std::atan2(x, y) - actorYaw);
            const float step = 6.0F * std::clamp(dt, 0.0F, 0.05F);
            return std::clamp(delta, -step, step);
        }

        // Standing still means no input and no native movement. Small input,
        // deceleration and stick drift all still count as moving.
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
    }

    namespace ADSTurn {
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
            yaw = Facing::Blend(yaw, target, dt, remaining);
            remaining = std::max(0.0F, remaining - dt);
            return remaining <= 0;
        }
    }

    namespace EquipLogic {
        inline bool FavoriteInput(bool keyboard, std::uint32_t code, std::string_view name) {
            if (keyboard && ((code >= 0x30 && code <= 0x39) || code == 0xBD || code == 0xBB ||
                             (code >= 2 && code <= 13)))
                return true;
            std::string lower(name);
            for (char &c : lower)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return lower.find("favorite") != std::string::npos ||
                   lower.find("hotkey") != std::string::npos ||
                   lower.find("quickkey") != std::string::npos;
        }
        inline double Extend(double until, double now, double duration) {
            return std::max(until, now + duration);
        }
    }

    // Mid-draw the game moves the character a fixed angle off the stick direction
    // whenever the stick has a sideways part (about 35 degrees, measured with
    // DrawTrace). The stick is turned against that, learned per stick direction.
    namespace DrawSteer {
        inline constexpr int kSectors = 8;
        // Stick direction in 45-degree sectors, 0 = forward, clockwise.
        inline int Sector(float x, float y) {
            const float angle = std::atan2(x, y); // -pi..pi, 0 forward, + right
            const int sector = static_cast<int>(
                std::floor((angle + std::numbers::pi_v<float> / kSectors) /
                           (2 * std::numbers::pi_v<float> / kSectors)));
            return ((sector % kSectors) + kSectors) % kSectors;
        }
        // Turns a stick vector clockwise by `angle` radians.
        inline std::array<float, 2> Rotate(float x, float y, float angle) {
            const float c = std::cos(angle), s = std::sin(angle);
            return {x * c + y * s, y * c - x * s};
        }
        // One step against the measured error (actual minus wanted move yaw).
        inline float Learn(float correction, float error, float gain = 0.08F,
                           float limit = 0.9F) {
            if (!std::isfinite(error) || std::abs(error) > 1.2F)
                return correction;
            return std::clamp(correction - gain * error, -limit, limit);
        }
    }

    // Hip-fire torso twist: the body keeps (close to) the move direction and the
    // spine turns the upper body the rest of the way to the camera.
    namespace Twist {
        using Mat3 = std::array<std::array<float, 3>, 3>; // [row][column]

        // Body yaw while hip firing: `wanted` (move direction, or the current body
        // yaw when standing) kept within `limit` of the camera.
        inline float BodyTarget(float cameraYaw, float wanted, float bodyYaw, float limit) {
            float relative = Logic::SignedYaw(wanted - cameraYaw);
            // Straight behind the camera either side would do; stay on the body's side
            // so the target can't flip between them.
            if (std::abs(relative) > 2.6F)
                relative = Logic::SignedYaw(bodyYaw - cameraYaw) >= 0 ? std::abs(relative)
                                                                      : -std::abs(relative);
            return Logic::Normalize(cameraYaw + std::clamp(relative, -limit, limit));
        }

        // Legs swinging to a new side while hip firing: an eased turn with a speed
        // cap, so a full left-right switch takes about a third of a second.
        inline float TurnBody(float current, float target, float dt) {
            if (!std::isfinite(current) || !std::isfinite(target) || !std::isfinite(dt))
                return current;
            dt = std::clamp(dt, 0.0F, 0.05F);
            const float gap = Logic::SignedYaw(target - current);
            const float step = std::clamp(gap * (1 - std::exp(-10.0F * dt)), -7.0F * dt, 7.0F * dt);
            return Logic::Normalize(current + step);
        }

        inline float Approach(float current, float target, float rate, float dt) {
            if (!std::isfinite(current) || !std::isfinite(target) || !std::isfinite(dt))
                return 0;
            return current + (target - current) * (1 - std::exp(-rate * std::clamp(dt, 0.0F, 0.1F)));
        }

        inline Mat3 Multiply(const Mat3 &a, const Mat3 &b) {
            Mat3 out{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
            return out;
        }
        inline Mat3 Transpose(const Mat3 &a) {
            return {{{a[0][0], a[1][0], a[2][0]}, {a[0][1], a[1][1], a[2][1]}, {a[0][2], a[1][2], a[2][2]}}};
        }
        // Turn about world up by `yaw` (game convention: +yaw turns +Y toward +X).
        inline Mat3 YawRotation(float yaw) {
            const float c = std::cos(yaw), s = std::sin(yaw);
            return {{{c, s, 0}, {-s, c, 0}, {0, 0, 1}}};
        }
        // New local rotation for a node whose parent's world rotation is `parent`,
        // so the node (and everything under it) turns `yaw` about world up.
        // world = parent * local, column vectors.
        inline Mat3 TurnLocal(const Mat3 &parent, const Mat3 &local, float yaw) {
            return Multiply(Multiply(Transpose(parent), Multiply(YawRotation(yaw), parent)), local);
        }
    }

    namespace LockMath {
        // Camera local forward (0,1,0) rotated by a world quaternion.
        inline std::array<float, 3> Forward(float w, float x, float y, float z) {
            return {-2 * w * z + 2 * x * y, 1 - 2 * x * x - 2 * z * z, 2 * w * x + 2 * y * z};
        }
        inline float Turn(float current, float desired, float response, float dt) {
            if (!std::isfinite(current) || !std::isfinite(desired) || !std::isfinite(dt))
                return current;
            const float alpha = 1 - std::exp(-response * std::clamp(dt, 0.0F, 0.05F));
            return current + Logic::SignedYaw(desired - current) * alpha;
        }
        // Lower is better; infinity rejects. Direction picks the side of the current target.
        inline float Score(float yaw, float anchor, int direction) {
            if (!std::isfinite(yaw) || !std::isfinite(anchor) || std::abs(yaw) > 1.40F)
                return std::numeric_limits<float>::infinity();
            const float side = yaw - anchor;
            if ((direction > 0 && side <= 0.02F) || (direction < 0 && side >= -0.02F))
                return std::numeric_limits<float>::infinity();
            return std::abs(direction ? side : yaw);
        }
        // Automatic handoff ranks by distance in every direction; manual picks use the view cone.
        inline float SelectionScore(bool nearest, float yaw, float anchor, int direction, float dx,
                                    float dy, float dz) {
            return nearest ? std::hypot(dx, dy, dz) : Score(yaw, anchor, direction);
        }
        // Mouse IDs arrive device-local, packed, or combined depending on the source.
        // Only mouse events qualify so keyboard/gamepad aliases are never swallowed.
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
        // Correction from the real camera forward, so the shoulder offset is included.
        // A new target can be behind the player, so the per-frame step is capped.
        inline std::array<float, 2> CameraCorrection(float dx, float dy, float dz, float fx, float fy,
                                                     float fz, float response, float dt) {
            if (!std::isfinite(dx + dy + dz + fx + fy + fz + dt) || std::hypot(dx, dy) < 0.01F ||
                std::hypot(fx, fy) < 0.01F)
                return {0, 0};
            const float alpha = 1 - std::exp(-response * std::clamp(dt, 0.0F, 0.05F));
            const float yaw = Logic::SignedYaw(std::atan2(dx, dy) - std::atan2(fx, fy));
            const float pitch =
                -std::atan2(dz, std::hypot(dx, dy)) + std::atan2(fz, std::hypot(fx, fy));
            const float maxStep = 3.0F * std::clamp(dt, 0.0F, 0.05F);
            return {std::clamp(yaw * alpha, -maxStep, maxStep),
                    std::clamp(pitch * alpha, -maxStep, maxStep)};
        }
    }

    // Target-lock key choices. The order of each list matches its MCM dropdown.
    namespace LockKeys {
        enum class Device : std::uint8_t { None, Mouse, Keyboard, Gamepad };
        struct Key {
            Device device;
            std::uint32_t code; // mouse button index, Windows VK code or XInput mask
        };
        inline constexpr Key keyboardMouse[]{
            {Device::Mouse, 2}, // middle button
            {Device::Mouse, 3}, // mouse 4
            {Device::Mouse, 4}, // mouse 5
            {Device::Keyboard, 'B'}, {Device::Keyboard, 'H'}, {Device::Keyboard, 'K'},
            {Device::Keyboard, 'L'}, {Device::Keyboard, 'N'}, {Device::Keyboard, 'O'},
            {Device::Keyboard, 'P'}, {Device::Keyboard, 'U'}, {Device::Keyboard, 'X'},
            {Device::Keyboard, 'Y'}, {Device::Keyboard, 'Z'},
            {Device::None, 0},
        };
        inline constexpr Key gamepad[]{
            {Device::Gamepad, 0x0080}, // right stick click
            {Device::Gamepad, 0x0040}, // left stick click
            {Device::Gamepad, 0x0200}, // right bumper
            {Device::Gamepad, 0x0100}, // left bumper
            {Device::Gamepad, 0x1000}, // A
            {Device::Gamepad, 0x2000}, // B
            {Device::Gamepad, 0x4000}, // X
            {Device::Gamepad, 0x8000}, // Y
            {Device::Gamepad, 0x0001}, // d-pad up
            {Device::Gamepad, 0x0002}, // d-pad down
            {Device::Gamepad, 0x0004}, // d-pad left
            {Device::Gamepad, 0x0008}, // d-pad right
            {Device::None, 0},
        };
        // Out-of-range choices fall back to the first entry (the default).
        template <std::size_t N> constexpr Key Pick(const Key (&list)[N], int index) noexcept {
            return index >= 0 && static_cast<std::size_t>(index) < N ? list[index] : list[0];
        }
        // Mouse and gamepad IDs can also arrive with the 0x10000 device bit set.
        constexpr bool Matches(Key key, Device device, std::uint32_t code) noexcept {
            if (key.device == Device::None || key.device != device)
                return false;
            return code == key.code || (device != Device::Keyboard && code == (0x10000 | key.code));
        }
    }

    namespace ControllerInput {
        constexpr bool LookStick(bool gamepad, std::uint32_t code) noexcept {
            return gamepad && (code == 12 || code == 0x1000C);
        }

        // One switch per flick. The stick must return to neutral first, including
        // right after acquiring a lock, so the look input that found the enemy
        // can't immediately switch away from it.
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

    // Smooths the controller movement heading with a 65 ms time constant.
    class ControllerSteering {
        float heading = 0;
        bool valid = false;

      public:
        void Reset() noexcept { valid = false; }
        float Update(float x, float y, float dt) noexcept {
            const float target = std::atan2(x, y);
            if (!std::isfinite(x) || !std::isfinite(y) || std::hypot(x, y) <= 0.10F) {
                Reset();
                return 0;
            }
            if (!valid || !std::isfinite(dt) || dt <= 0 || dt > 0.25F) {
                heading = target;
                valid = true;
            } else {
                heading = Logic::SignedYaw(heading + Logic::SignedYaw(target - heading) *
                                                         (-std::expm1(-dt / 0.065F)));
            }
            return heading;
        }
    };

    namespace LookSensitivity {
        struct Axes {
            float x = 1.0F, y = 1.0F;
        };
        struct Profile {
            Axes holstered, drawn, ads;
            const Axes &Select(bool aiming, bool armed) const noexcept {
                return aiming ? ads : armed ? drawn : holstered;
            }
        };
        inline float Parse(const wchar_t *text, float fallback) noexcept {
            return Logic::ParseNumber(text, fallback, 0.05F, 5.0F);
        }
    }

    // Shows the real weapon state to native/third-party input handlers for one call.
    // If the handler changes the state itself, that change is kept.
    template <class Actor> class WeaponInputScope {
        using State = decltype(Actor::weaponState);
        Actor &actor;
        bool &spoof;
        bool &native;
        State previousWeapon, realWeapon;
        bool previousSpoof, previousNative;

      public:
        WeaponInputScope(Actor &value, bool &spoofFlag, bool &nativeFlag, State real, bool pov)
            : actor(value), spoof(spoofFlag), native(nativeFlag), previousWeapon(value.weaponState),
              realWeapon(real), previousSpoof(spoofFlag), previousNative(nativeFlag) {
            actor.weaponState = real;
            spoof = false;
            native = native || pov;
        }
        WeaponInputScope(const WeaponInputScope &) = delete;
        WeaponInputScope &operator=(const WeaponInputScope &) = delete;
        ~WeaponInputScope() {
            if (previousSpoof && actor.weaponState == realWeapon)
                actor.weaponState = previousWeapon;
            spoof = previousSpoof;
            native = previousNative;
        }
    };
}
