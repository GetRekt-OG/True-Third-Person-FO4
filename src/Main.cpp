#include "PCH.hpp"
#include "Input.hpp"
#include "TargetLock.hpp"
#include "TorsoTwist.hpp"
#include "LockMarker.hpp"
#include "McmReset.hpp"
#include "NativeHooks.hpp"
#include "WaterWeaponVisibility.hpp"
#include "RE/B/BipedAnim.hpp"
#include "RE/P/Projectile.hpp"

namespace TrueThirdPerson {
    inline constexpr REX::Version kPluginVersion{0, 2, 5, 0};

    // Every runtime listed here needs its own table in BuiltinAddresses.hpp.
    inline constexpr std::array kTargetRuntimes{
        REL::CreateRuntime(1, 10, 163, 0), REL::CreateRuntime(1, 10, 980, 0),
        REL::CreateRuntime(1, 10, 984, 0), REL::CreateRuntime(1, 11, 191, 0),
        REL::CreateRuntime(1, 11, 221, 0), REL::CreateRuntime(1, 11, 240, 0)};

    constexpr bool SupportedRuntime(REX::Version version) {
        for (const auto runtime : kTargetRuntimes)
            if (runtime.GetVersion() == version)
                return true;
        return false;
    }

    // While a native camera callback runs, the camera may see a spoofed
    // "holstered" weapon state; the real one is kept here.
    thread_local bool g_cameraWeaponSpoofActive = false;
    thread_local RE::WEAPON_STATE g_cameraRealWeaponState = RE::WEAPON_STATE::kSheathed;
    thread_local bool g_hipFireCameraScoped = false;
    thread_local bool g_nativePOVInput = false;

    double g_hipFireUntil = 0;
    bool g_hipFireWasActive = false;
    unsigned g_hipFireReleaseFrames = 0;
    bool g_fireBodyValid = false;
    float g_fireBodyYaw = 0, g_returnYaw = 0, g_returnRemaining = 0;
    bool g_favoriteCameraHeld = false;
    bool g_favoriteFromMelee = false;
    bool g_sprintPaused = false;       // TTP stopped a sprint for a switch from melee
    bool g_sprintPausedToggle = false; // ...and toggle-sprint was on at the time

    // Weapon drawn in third person with the independent camera enabled.
    bool IndependentFacingActive(const RE::PlayerCharacter *player) {
        if (!TTPActive() || !Gameplay() || VATSMenuOpen() || !player || !WeaponDrawn(player))
            return false;
        const auto *filter = static_cast<const RE::IMovementPlayerControlsFilter *>(player);
        return filter && filter->IsInThirdPerson();
    }

    bool HipFireContext(const RE::PlayerCharacter *player) {
        return g_config.hipFireFacing && IndependentFacingActive(player) &&
               player->weaponState == RE::WEAPON_STATE::kDrawn && !IronSightsActive() &&
               Now() >= EquipGuard::until && GunModelReady(player);
    }

    bool HipFireMovementActive(const RE::PlayerCharacter *player) {
        return HipFireContext(player) && Now() < g_hipFireUntil;
    }

    // A throw: the Melee key held for a grenade or mine (a tap is a weapon bash),
    // while it is down and for a moment after. From standing, the body turns to
    // the camera with the legs planted; moving, the spine twists to it (see
    // FaceThrow). ThrowAim sends the grenade itself to the crosshair.
    namespace ThrowFacing {
        // The key sends an event every frame while it is down; `heldAt` is the
        // last one. A release the game never passes on (Alt-Tab: Melee is Alt)
        // can't leave it stuck on, which held the body to the camera for good.
        inline double heldAt = 0;
        inline double until = 0;
        // Standing throw: legs kept at legsYaw, `legs` radians from the body.
        inline bool was = false, standing = false;
        // This frame: TTP holds the body to the camera (a throw from standing),
        // or the spine twists to it while you keep moving.
        inline bool steering = false, twisting = false;
        inline float legsYaw = 0, legs = 0;
        inline void Button(const RE::ButtonEvent *button) {
            if (std::string_view(button->QUserEvent().c_str()) != "Melee")
                return;
            if (button->QPressed()) {
                heldAt = Now();
            } else if (button->QReleased() && heldAt > 0) {
                heldAt = 0;
                until = Now() + 1.2;
            }
        }
        inline bool Held() {
            return Now() - heldAt < 0.3;
        }
        inline void Reset() {
            heldAt = until = 0;
            was = standing = steering = twisting = false;
            legs = 0;
            TorsoTwist::appliedLegs = 0;
        }
        inline bool Active(const RE::PlayerCharacter *player) {
            if (!Held() && Now() >= until)
                return false;
            const auto *camera = RE::PlayerCamera::GetSingleton();
            return TTPActive() && Gameplay() && !VATSMenuOpen() && player && camera &&
                   camera->IsStateActive(RE::CameraStates::kThirdPerson) && !IronSightsActive() &&
                   !TargetLock::Active() && player->lifeState == RE::ACTOR_LIFE_STATE::kAlive;
        }
    }

    // Hip fire with the upper body twisting to the camera (not in power armor,
    // which turns through the movement hook, nor during a throw).
    bool HipFireWithTwist(const RE::PlayerCharacter *player) {
        return TorsoTwist::Enabled() && HipFireMovementActive(player) && !InPowerArmor(player) &&
               player->gunState != RE::GUN_STATE::kRelaxed && !ThrowFacing::steering;
    }

    bool FavoriteSwitchInProgress(const RE::PlayerCharacter *player) {
        if (!g_favoriteCameraHeld || !player)
            return false;
        // Inside a camera callback the state reads "sheathed" because we set it;
        // that says nothing about whether the equip animation has finished.
        if (g_cameraWeaponSpoofActive)
            return true;
        switch (player->weaponState) {
        case RE::WEAPON_STATE::kWantToDraw:
        case RE::WEAPON_STATE::kDrawing:
        case RE::WEAPON_STATE::kWantToSheathe:
        case RE::WEAPON_STATE::kSheathing:
            return true;
        case RE::WEAPON_STATE::kSheathed:
        case RE::WEAPON_STATE::kDrawn:
            return Now() < EquipGuard::until;
        default:
            return false;
        }
    }

    // A favorites switch from a melee weapon can't draw mid-sprint (the melee
    // sprint pose holds until the sprint ends), so run through the switch instead.
    void SuppressSprintForMeleeSwitch(RE::PlayerCharacter *player) {
        if (!player)
            return;
        auto *state = static_cast<RE::ActorState *>(player);
        if (g_favoriteFromMelee && FavoriteSwitchInProgress(player)) {
            if (state->sprinting || player->sprintToggled) {
                if (!g_sprintPaused) {
                    g_sprintPaused = true;
                    g_sprintPausedToggle = player->sprintToggled;
                    TTPDiagnostic::Trace("Favorites: sprint paused for a switch from melee");
                }
                state->sprinting = 0;
                player->sprintToggled = false;
            }
            return;
        }
        // Switch finished: pick the sprint back up if the key is still held
        // (or toggle-sprint was on) and the player is still moving.
        if (!g_sprintPaused)
            return;
        g_sprintPaused = false;
        const auto *controls = RE::PlayerControls::GetSingleton();
        const bool moving = controls && std::hypot(controls->data.moveInputVec.x,
                                                   controls->data.moveInputVec.y) > 0.5F;
        if (moving && (g_sprintPausedToggle || EquipGuard::sprintHeld)) {
            player->sprintToggled = g_sprintPausedToggle;
            state->sprinting = 1;
            TTPDiagnostic::Trace("Favorites: sprint resumed after the switch");
        }
    }

    // Movement is camera-relative and the body turns toward the stick direction.
    bool IndependentLocomotionActive(RE::PlayerCharacter *player) {
        const auto *camera = RE::PlayerCamera::GetSingleton();
        return TTPActive() && Gameplay() && !VATSMenuOpen() && camera &&
               camera->IsStateActive(RE::CameraStates::kThirdPerson) &&
               g_cameraHooksInstalled.load() && g_directionalHooksInstalled.load() &&
               g_controlsHookInstalled.load() && !HipFireMovementActive(player) &&
               !TargetLock::Active() && player && !ThrowFacing::steering &&
               player->lifeState == RE::ACTOR_LIFE_STATE::kAlive && !player->swimming &&
               player->DoGetCharacterState() != RE::IMovementState::CHARACTER_STATE::kSwimming &&
               // Every state with a weapon out or on its way in or out. Leaving out the
               // draw lets vanilla face the camera for the draw, then snap back.
               (player->weaponState != RE::WEAPON_STATE::kSheathed ||
                FavoriteSwitchInProgress(player)) &&
               !IronSightsActive();
    }
}

#include "CameraOrbit.hpp"
#include "ADSStateHook.hpp"

namespace TrueThirdPerson {
    // The game sees the stick as "forward at full magnitude" so the body runs
    // the way it faces; the real input is kept here for choosing that facing.
    // It is put back once Actor::Update has consumed the remapped value.
    // Mid-draw the stick stays camera-relative but is turned by the learned
    // draw correction (DrawSteer).
    struct DirectionalInput {
        static inline RE::NiPoint2 raw{0.0F, 0.0F};
        static inline RE::NiPoint2 written{0.0F, 0.0F};
        static inline bool valid = false;
        // Starting values from the DrawTrace logs: about 35 degrees for every
        // stick direction with a sideways part. Refined while playing.
        static inline float drawCorrection[DrawSteer::kSectors]{0.0F, -0.6F, -0.6F, -0.6F,
                                                                0.0F, 0.6F,  0.6F,  0.6F};
        static inline RE::NiPoint3 lastPosition{};
        static inline double lastSample = 0;
        static inline bool drawSteered = false; // a corrected mid-draw input went out
        static inline RE::NiPoint2 drawInput{};
        static inline RE::WEAPON_STATE lastState = RE::WEAPON_STATE::kSheathed;
        static inline double settleUntil = 0;
        static inline bool wasActive = false;
        static inline bool settling = false; // this frame's input is the settle mapping
        static inline unsigned resumes = 0; // handovers back from hip fire, ADS, a lock
        // What Prepare did this frame, for the draw trace: N not active, D mid-draw
        // (camera-relative, corrected), S settling (stick relative to the body),
        // R remapped to forward.
        static inline char mode = 'N';

        // Body already faces the stick direction (within ~10 degrees).
        static bool BodyAligned(RE::PlayerCharacter *player, RE::NiPoint2 input) {
            float cameraYaw = 0;
            if (std::hypot(input.x, input.y) < 0.1F ||
                !CameraOrbit::ReadCameraRootWorldYaw(cameraYaw))
                return true;
            const float desired = cameraYaw + std::atan2(input.x, input.y);
            return std::abs(Logic::SignedYaw(desired - player->data.angle.z)) < 0.17F;
        }

        static void Restore() {
            auto *controls = RE::PlayerControls::GetSingleton();
            if (valid && controls && controls->data.moveInputVec.x == written.x &&
                controls->data.moveInputVec.y == written.y)
                controls->data.moveInputVec = raw;
            valid = false;
        }
        static void Write(RE::PlayerControls *controls, RE::NiPoint2 value) {
            raw = controls->data.moveInputVec;
            written = value;
            valid = true;
            controls->data.moveInputVec = value;
        }

        // Once per player update, before Prepare: compares where the last frame's
        // mid-draw movement went with where the stick pointed, and adjusts the
        // correction for that stick direction.
        static void LearnDrawCorrection(RE::PlayerCharacter *player) {
            const auto position = player->GetPosition();
            const double now = Now();
            const double dt = now - lastSample;
            const float movedX = position.x - lastPosition.x, movedY = position.y - lastPosition.y;
            lastPosition = position;
            lastSample = now;
            const bool steered = std::exchange(drawSteered, false);
            if (!steered || dt <= 0.001 || dt > 0.1)
                return;
            const auto input = drawInput;
            const float magnitude = std::hypot(input.x, input.y);
            const float speed = std::hypot(movedX, movedY) / static_cast<float>(dt);
            float cameraYaw = 0;
            if (magnitude < 0.5F || speed < 150 || !CameraOrbit::ReadCameraRootWorldYaw(cameraYaw) ||
                player->DoGetCharacterState() != RE::IMovementState::CHARACTER_STATE::kOnGround)
                return;
            const float wanted = cameraYaw + std::atan2(input.x, input.y);
            const float error = Logic::SignedYaw(std::atan2(movedX, movedY) - wanted);
            auto &correction = drawCorrection[DrawSteer::Sector(input.x, input.y)];
            correction = DrawSteer::Learn(correction, error);
        }
        static void Prepare(RE::PlayerCharacter *player) {
            Restore();
            mode = 'N';
            settling = false;
            auto *controls = RE::PlayerControls::GetSingleton();
            if (controls)
                g_moveStick = controls->data.moveInputVec;
            if (controls && player && HipFireWithTwist(player)) {
                // The body isn't facing the camera during a twisted hip fire, so
                // give the game the stick relative to the body (it strafes).
                wasActive = false;
                const auto input = controls->data.moveInputVec;
                const float magnitude = std::hypot(input.x, input.y);
                float cameraYaw = 0;
                if (magnitude >= 0.1F && std::isfinite(magnitude) &&
                    CameraOrbit::ReadCameraRootWorldYaw(cameraYaw)) {
                    mode = 'H';
                    const float relative = Logic::SignedYaw(
                        cameraYaw + std::atan2(input.x, input.y) - player->data.angle.z);
                    Write(controls,
                          {std::sin(relative) * magnitude, std::cos(relative) * magnitude});
                }
                return;
            }
            if (!controls || !IndependentLocomotionActive(player)) {
                wasActive = false;
                return;
            }
            // Mid-draw the game still moves camera-relative, like holstered. Remapping
            // to "forward" there slides the character sideways; the body still turns.
            const auto state = player->weaponState;
            const bool drawing =
                state == RE::WEAPON_STATE::kWantToDraw || state == RE::WEAPON_STATE::kDrawing;
            const bool favorites = FavoriteSwitchInProgress(player);
            // Handover into independent movement with a weapon out: right after a
            // draw, and whenever it resumes (hip fire, ADS or a lock ending). The body
            // may face far from the stick then; see the settle step below.
            if (state == RE::WEAPON_STATE::kDrawn && !favorites &&
                (lastState == RE::WEAPON_STATE::kWantToDraw ||
                 lastState == RE::WEAPON_STATE::kDrawing || !wasActive)) {
                settleUntil = Now() + 1.0;
                if (!wasActive)
                    ++resumes;
            }
            lastState = state;
            wasActive = true;
            if (drawing && !favorites) {
                mode = 'D';
                const auto input = controls->data.moveInputVec;
                if (std::hypot(input.x, input.y) < 0.1F)
                    return;
                const auto turned = DrawSteer::Rotate(
                    input.x, input.y, drawCorrection[DrawSteer::Sector(input.x, input.y)]);
                if (std::isfinite(turned[0]) && std::isfinite(turned[1])) {
                    Write(controls, {turned[0], turned[1]});
                    drawSteered = true;
                    drawInput = input;
                }
                return;
            }
            const auto input = controls->data.moveInputVec;
            const float magnitude = std::hypot(input.x, input.y);
            if (!std::isfinite(magnitude))
                return;
            // Settle: while the body is still turning toward the stick, give the game
            // the stick direction relative to the body and keep it in combat stance
            // (see Stance), so it strafes where the stick points instead of running
            // forward along the turn. Outside combat stance the game only runs the
            // way the body faces, which made the character go the wrong way.
            float cameraYaw = 0;
            if (state == RE::WEAPON_STATE::kDrawn && Now() < settleUntil && magnitude >= 0.1F &&
                player->gunState != RE::GUN_STATE::kRelaxed && !BodyAligned(player, input) &&
                CameraOrbit::ReadCameraRootWorldYaw(cameraYaw)) {
                mode = 'S';
                settling = true;
                const float relative = Logic::SignedYaw(
                    cameraYaw + std::atan2(input.x, input.y) - player->data.angle.z);
                Write(controls, {std::sin(relative) * magnitude, std::cos(relative) * magnitude});
                return;
            }
            mode = 'R';
            Write(controls, {0.0F, magnitude});
        }
    };

    // [Debug] DrawTrace=1: for 1.5 s after each weapon draw starts, records per
    // frame what the stick asks for, where the body faces and where the character
    // actually moves. Kept in memory and written in one go when the window ends.
    struct DrawTrace {
        static inline bool active = false;
        static inline double start = 0, lastTime = 0;
        static inline RE::NiPoint3 lastPosition{};
        static inline RE::WEAPON_STATE lastState = RE::WEAPON_STATE::kSheathed;
        static inline std::vector<std::string> lines;
        static inline float worstSide = 0, worstAt = 0;
        static inline unsigned seenResumes = 0;

        static const char *StateName(RE::WEAPON_STATE state) {
            switch (state) {
            case RE::WEAPON_STATE::kSheathed: return "Sheathed";
            case RE::WEAPON_STATE::kWantToDraw: return "WantDraw";
            case RE::WEAPON_STATE::kDrawing: return "Drawing";
            case RE::WEAPON_STATE::kDrawn: return "Drawn";
            case RE::WEAPON_STATE::kWantToSheathe: return "WantSheathe";
            case RE::WEAPON_STATE::kSheathing: return "Sheathing";
            default: return "?";
            }
        }
        static float Degrees(float radians) {
            return Logic::SignedYaw(radians) * 180.0F / std::numbers::pi_v<float>;
        }

        static void Finish() {
            char summary[200]{};
            std::snprintf(summary, sizeof(summary),
                          "Draw trace end: %zu frames, largest sideways drift %.1f deg at %.0f ms",
                          lines.size() - 1, worstSide, worstAt);
            lines.emplace_back(summary);
            TTPDiagnostic::TraceLines(lines);
            lines.clear();
            active = false;
        }

        // Every player update, after DirectionalInput::Prepare and before the native update.
        static void Sample(RE::PlayerCharacter *player) {
            if (!g_config.drawTrace || !player) {
                if (active)
                    Finish();
                lastState = player ? player->weaponState : RE::WEAPON_STATE::kSheathed;
                return;
            }
            const auto state = player->weaponState;
            const double now = Now();
            const auto position = player->GetPosition();
            const bool drawStart =
                (state == RE::WEAPON_STATE::kWantToDraw || state == RE::WEAPON_STATE::kDrawing) &&
                lastState == RE::WEAPON_STATE::kSheathed;
            const bool resumed = DirectionalInput::resumes != seenResumes;
            seenResumes = DirectionalInput::resumes;
            lastState = state;
            if ((drawStart || resumed) && !active) {
                active = true;
                start = now;
                worstSide = worstAt = 0;
                lines.clear();
                lines.reserve(128);
                char header[400]{};
                std::snprintf(header, sizeof(header),
                              "Draw trace start (%s%s, TTP %s, favorites %s). Columns: ms, weapon "
                              "state, stick x/y, input mode, body yaw, camera yaw, wanted move "
                              "yaw, actual move yaw, speed, sideways drift, draw correction, gun state "
                              "(1 relaxed, 3 alert, 7 firing)",
                              StateName(state), drawStart ? "" : ", movement resumed after hip fire/ADS/lock",
                              g_config.enabled ? "on" : "off",
                              FavoriteSwitchInProgress(player) ? "yes" : "no");
                lines.emplace_back(header);
            } else if (!active) {
                lastPosition = position;
                lastTime = now;
                return;
            }
            const float ms = static_cast<float>((now - start) * 1000.0);
            const float dt = static_cast<float>(now - lastTime);
            const float dx = position.x - lastPosition.x, dy = position.y - lastPosition.y;
            lastPosition = position;
            lastTime = now;
            const auto *controls = RE::PlayerControls::GetSingleton();
            const RE::NiPoint2 stick = DirectionalInput::valid ? DirectionalInput::raw
                                       : controls                ? controls->data.moveInputVec
                                                                 : RE::NiPoint2{};
            float cameraYaw = 0;
            const bool haveCamera = CameraOrbit::ReadCameraRootWorldYaw(cameraYaw);
            const bool moving = std::hypot(stick.x, stick.y) > 0.1F;
            const float want = cameraYaw + std::atan2(stick.x, stick.y);
            const float speed = dt > 0.0001F ? std::hypot(dx, dy) / dt : 0;
            const float actual = std::atan2(dx, dy);
            const bool measured = moving && haveCamera && speed > 20 && ms > 0;
            const float side = measured ? Degrees(actual - want) : 0;
            if (measured && std::abs(side) > std::abs(worstSide)) {
                worstSide = side;
                worstAt = ms;
            }
            char drift[16] = "-";
            if (measured)
                std::snprintf(drift, sizeof(drift), "%.1f", side);
            char line[200]{};
            std::snprintf(line, sizeof(line),
                          "%5.0f %-11s %5.2f %5.2f %c body %7.1f cam %7.1f want %7.1f move %7.1f "
                          "spd %4.0f side %s corr %.0f gun %u",
                          ms, StateName(state), stick.x, stick.y, DirectionalInput::mode,
                          Degrees(player->data.angle.z), haveCamera ? Degrees(cameraYaw) : 0.0F,
                          moving && haveCamera ? Degrees(want) : 0.0F,
                          speed > 20 ? Degrees(actual) : 0.0F, speed, drift,
                          moving ? Degrees(DirectionalInput::drawCorrection[DrawSteer::Sector(
                                       stick.x, stick.y)])
                                 : 0.0F,
                          static_cast<unsigned>(player->gunState));
            lines.emplace_back(line);
            if (ms >= 1500 || lines.size() >= 400)
                Finish();
        }
    };

    // Turns the body toward the movement direction. Only the yaw delta changes;
    // translation, jumping and the camera stay with their normal code paths.
    struct DirectionalHooks {
        static inline ControllerSteering steering;

        static void Movement(RE::PlayerCharacter *self, float dt, RE::NiPoint3 &move,
                             RE::NiPoint3 &angle) {
            Profile::Scope timing;
            const bool vats = self == RE::PlayerCharacter::GetSingleton() && VATSMenuOpen();
            if (vats)
                DirectionalInput::Restore();
            Profile::Call(movementOriginal, self, dt, move, angle);
            if (self != RE::PlayerCharacter::GetSingleton())
                return;
            // VATS owns facing and keeps native strafe/backward movement.
            if (vats) {
                steering.Reset();
                return;
            }
            if (!IndependentLocomotionActive(self) || !LookDevice::movementController)
                steering.Reset();
            if (g_directionalHooksInstalled.load() && InPowerArmor(self) &&
                HipFireMovementActive(self)) {
                FacePowerArmorHipFire(self, dt, angle);
                return;
            }
            if (!IndependentLocomotionActive(self))
                return;
            auto *controls = RE::PlayerControls::GetSingleton();
            if (!controls)
                return;
            const auto input =
                DirectionalInput::valid ? DirectionalInput::raw : controls->data.moveInputVec;
            float yaw = 0.0F;
            if (!CameraOrbit::ReadCameraRootWorldYaw(yaw))
                return;
            if (ADSTurn::pending) {
                steering.Reset();
                angle.z = 0;
                return;
            }
            auto direction = input;
            if (LookDevice::movementController) {
                const float heading = steering.Update(input.x, input.y, dt);
                const float magnitude = std::hypot(input.x, input.y);
                direction.x = std::sin(heading) * magnitude;
                direction.y = std::cos(heading) * magnitude;
            }
            angle.z = Facing::MoveTurn(direction.x, direction.y, yaw, self->data.angle.z, dt);
        }

        // Power armor hip fire turns through here so locomotion keeps its root motion.
        static void FacePowerArmorHipFire(RE::PlayerCharacter *self, float dt, RE::NiPoint3 &angle) {
            auto *third = ActiveThirdPerson();
            if (!third)
                return;
            const float target = CameraOrbit::g_savedFreeRotationValid
                                     ? CameraOrbit::g_savedFreeRotation.x
                                     : third->freeRotation.x;
            if (!std::isfinite(target) || !std::isfinite(self->data.angle.z) ||
                !std::isfinite(dt) || dt <= 0)
                return;
            const float next = g_config.smoothHipFire ? Facing::Blend(self->data.angle.z, target, dt)
                                                      : Logic::Normalize(target);
            angle.z = Logic::SignedYaw(next - self->data.angle.z);
        }

        // Combat stance means the game strafes relative to the body; outside it,
        // the character runs the way the body faces. Independent movement wants the
        // latter. Hip fire and the settle step want strafing even once the gun has
        // relaxed (a relaxed gun drops the game out of combat stance, and moving
        // back and to the side then ran the character the opposite way).
        static bool Stance(const RE::IMovementPlayerControlsFilter *self) {
            Profile::Scope timing;
            const bool native = Profile::Call(stanceOriginal, self);
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player || self != static_cast<const RE::IMovementPlayerControlsFilter *>(player))
                return native;
            if (player->weaponState == RE::WEAPON_STATE::kDrawn &&
                player->gunState != RE::GUN_STATE::kRelaxed &&
                (DirectionalInput::settling ||
                 (HipFireMovementActive(player) && !InPowerArmor(player))))
                return true;
            if (IndependentLocomotionActive(player))
                return false;
            return native;
        }

        static bool Install() {
            if (g_directionalHooksInstalled.load())
                return true;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player)
                return false;
            // Both hooks must be in place before either takes effect.
            static bool movementInstalled = false;
            static bool stanceInstalled = false;
            if (!movementInstalled) {
                auto hooks = HookBatch::For(player);
                hooks.Add(0x125, Movement, movementOriginal);
                movementInstalled = hooks.Commit();
            }
            if (!stanceInstalled) {
                auto hooks = HookBatch::For(static_cast<RE::IMovementPlayerControlsFilter *>(player));
                hooks.Add(0x03, Stance, stanceOriginal);
                stanceInstalled = hooks.Commit();
            }
            g_directionalHooksInstalled.store(movementInstalled && stanceInstalled);
            return g_directionalHooksInstalled.load();
        }

        static inline decltype(&Movement) movementOriginal = nullptr;
        static inline decltype(&Stance) stanceOriginal = nullptr;
    };

    // Clears everything that depends on the current scene or camera session.
    // Weapon switches that don't come from a favorites key (a wheel menu, a
    // script, the Pip-Boy) get the same camera handling as a favorites switch.
    TargetLock::Equipped g_lastEquipped;
    bool g_lastEquippedKnown = false;

    void WatchEquipChanges(RE::PlayerCharacter *player) {
        const auto equipped = TargetLock::EquippedWeapon(player);
        if (g_lastEquippedKnown && equipped.weapon != g_lastEquipped.weapon &&
            !EquipGuard::Active()) {
            EquipGuard::Pause();
            CameraOrbit::BeginFavoriteCamera();
            g_favoriteFromMelee = g_lastEquipped.mode == TargetLock::Mode::Melee;
            TTPDiagnostic::Trace(g_favoriteCameraHeld
                                     ? "Favorites: weapon changed outside the hotkeys; camera held"
                                     : "Favorites: weapon changed outside the hotkeys");
        }
        g_lastEquipped = equipped;
        g_lastEquippedKnown = true;
    }

    // Grenades and mines take their launch direction from how the hand holds
    // them: a mine placed with the gun relaxed flew about 90 degrees left, from a
    // ready gun about 30 degrees right, holstered about 20 degrees right. What
    // moves them is the physics body, which already has its speed by the time
    // the projectile's own velocity is set (changing that, or the model before
    // release, did nothing). So the first frame a grenade or mine of the
    // player's that was in the hand has a velocity, the physics velocity is set
    // again, turned about the vertical toward the crosshair with the pitch kept,
    // through the game's own setter (the one Projectile::AddInitialVelocity
    // uses), and set again for a few frames after.
    //
    // That setter has no address in the tables. It is found from the grenade
    // class's vtable: slot 0xE7 (GrenadeProjectile::AddInitialVelocity) calls the
    // base version, which ends in the setter call; both calls are recognised by
    // the bytes that follow them (checked on OG and AE). No match, no correction.
    namespace ThrowAim {
        using SetVelocity = void (*)(RE::NiAVObject *, const RE::NiPoint3 *, bool);
        inline SetVelocity setVelocity = nullptr;
        inline bool searched = false;
        inline std::vector<std::uint32_t> held, done;

        // Target of the first E8 call followed by the bytes in `tail` (-1: any byte).
        inline std::uintptr_t CallBefore(std::uintptr_t start, std::size_t length,
                                         std::initializer_list<int> tail) {
            const auto *code = reinterpret_cast<const std::uint8_t *>(start);
            for (std::size_t i = 0; i + 5 + tail.size() <= length; ++i) {
                if (code[i] != 0xE8)
                    continue;
                bool match = true;
                std::size_t k = 0;
                for (const int b : tail) {
                    if (b >= 0 && code[i + 5 + k] != b) {
                        match = false;
                        break;
                    }
                    ++k;
                }
                if (!match)
                    continue;
                std::int32_t offset = 0;
                std::memcpy(&offset, code + i + 1, sizeof(offset));
                return start + i + 5 + offset;
            }
            return 0;
        }
        inline void Find(RE::Projectile *grenade) {
            searched = true;
            const auto *table = *reinterpret_cast<const std::uintptr_t *const *>(grenade);
            // call base; mov rax,[reg]; mov rcx,reg; call [rax+460h] (Get3D)
            const auto base = CallBefore(table[0xE7], 0x400,
                                         {0x48, 0x8B, -1, 0x48, 0x8B, -1, 0xFF, 0x90, 0x60, 0x04, 0x00, 0x00});
            // call setter; mov rax,[rbx]; mov edx,4; mov rcx,rbx; call [rax+68h]
            const auto setter = base ? CallBefore(base, 0x200, {0x48, 0x8B, 0x03, 0xBA, 0x04, 0x00, 0x00, 0x00,
                                                                0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x68})
                                     : 0;
            setVelocity = reinterpret_cast<SetVelocity>(setter);
            TTPDiagnostic::Trace(setter ? "Throw aim ready" : "Throw aim: velocity setter not found, off");
        }

        inline void Turn(RE::NiPoint3 &v, float delta) {
            const float c = std::cos(delta), s = std::sin(delta);
            v = {v.x * c + v.y * s, -v.x * s + v.y * c, v.z}; // +delta turns +Y toward +X
        }
        inline bool Listed(const std::vector<std::uint32_t> &list, std::uint32_t id) {
            return std::find(list.begin(), list.end(), id) != list.end();
        }

        // Heading from the projectile to where it should land: on the camera's
        // line of sight (seen from above), at the distance this throw carries.
        // Parallel to the camera would land it off to the side by the camera's
        // shoulder offset.
        inline float Heading(RE::PlayerCharacter *player, const RE::NiPoint3 &from, const RE::NiPoint3 &v) {
            float yaw = player->data.angle.z;
            CameraOrbit::ReadCameraRootWorldYaw(yaw);
            auto *camera = RE::PlayerCamera::GetSingleton();
            if (!camera || !camera->cameraRoot)
                return yaw;
            constexpr float gravity = 686.6F; // 9.81 m/s^2 in game units
            const float height = std::max(0.0F, from.z - player->GetPosition().z);
            const float time = (v.z + std::sqrt(v.z * v.z + 2 * gravity * height)) / gravity;
            const float range = std::clamp(std::hypot(v.x, v.y) * time, 50.0F, 4000.0F);
            const auto &eye = camera->cameraRoot->world.translation;
            const float fx = std::sin(yaw), fy = std::cos(yaw);
            const float dx = eye.x - from.x, dy = eye.y - from.y;
            const float b = dx * fx + dy * fy;
            const float disc = b * b - (dx * dx + dy * dy - range * range);
            if (!std::isfinite(disc) || disc < 0)
                return yaw;
            const float along = -b + std::sqrt(disc);
            return std::atan2(eye.x + fx * along - from.x, eye.y + fy * along - from.y);
        }

        // Released this throw: re-set for a few frames, in case the game applies
        // the hand's direction again after the first.
        struct Flight {
            std::uint32_t id;
            RE::NiPoint3 velocity;
            double at;
            int frames;
        };
        inline std::vector<Flight> flights;

        // Main thread, every player update after the native update.
        inline void Tick(RE::PlayerCharacter *player) {
            if (!ThrowFacing::Active(player) || !player->parentCell) {
                held.clear();
                done.clear();
                flights.clear();
                return;
            }
            const double now = Now();
            auto *cell = player->parentCell;
            std::scoped_lock guard(cell->spinLock);
            for (auto &ref : cell->references) {
                if (!ref || ref->GetFormType() != RE::FormType::kGrenadeProjectile)
                    continue;
                auto *projectile = static_cast<RE::Projectile *>(ref.get());
                const auto id = ref->GetFormID();
                if (projectile->shooter.get().get() != player)
                    continue;
                if (!searched)
                    Find(projectile);
                auto *node = projectile->Get3D();
                if (auto flight = std::find_if(flights.begin(), flights.end(),
                                               [&](const Flight &f) { return f.id == id; });
                    flight != flights.end()) {
                    if (flight->frames-- > 0 && node && setVelocity) {
                        auto v = flight->velocity;
                        v.z -= 686.6F * static_cast<float>(now - flight->at);
                        setVelocity(node, &v, true);
                    }
                    continue;
                }
                if (Listed(done, id))
                    continue;
                auto &v = projectile->velocity;
                if (!std::isfinite(v.x + v.y + v.z))
                    continue;
                // Only one that was in the hand during this throw, not an older
                // grenade still rolling.
                if (std::hypot(v.x, v.y, v.z) < 1) {
                    if (!Listed(held, id))
                        held.push_back(id);
                    continue;
                }
                if (!Listed(held, id))
                    continue;
                done.push_back(id);
                const float heading = Heading(player, ref->GetPosition(), v);
                const float delta = Logic::SignedYaw(heading - std::atan2(v.x, v.y));
                if (!node || !setVelocity || std::hypot(v.x, v.y) < 1 || !std::isfinite(delta) ||
                    std::abs(delta) < 0.02F)
                    continue;
                Turn(v, delta);
                Turn(projectile->movementDirection, delta);
                setVelocity(node, &v, true);
                flights.push_back({id, v, now, 3});
                char line[96]{};
                std::snprintf(line, sizeof(line), "Throw: projectile turned %.0f degrees to the crosshair",
                              delta * 180.0F / std::numbers::pi_v<float>);
                TTPDiagnostic::Trace(line);
            }
        }
    }

    // A throw from standing turns the body to the camera (hip fire's own facing
    // ran first, so this wins). The legs stay where they were, up to the twist
    // limit, and the spine turns the upper body forward, like the hip-fire twist;
    // afterwards the legs turn in place to catch up.
    void FaceThrow(RE::PlayerCharacter *player, float delta) {
        using namespace ThrowFacing;
        const bool active = Active(player);
        const bool moving = std::hypot(g_moveStick.x, g_moveStick.y) >= 0.1F;
        if (active && !was) {
            standing = !moving;
            legsYaw = player->data.angle.z;
            TTPDiagnostic::Trace(standing ? "Throw: upper body turned to the camera"
                                          : "Throw: moving, spine turned to the camera");
        }
        was = active;
        // Moving, you keep moving where you steer and the spine twists to the
        // camera (TorsoTwist, as in hip fire); the throw itself is aimed in ThrowAim.
        steering = active && !moving;
        twisting = active && moving && TorsoTwist::Enabled();
        standing = standing && steering && TorsoTwist::Enabled();
        float yaw = 0;
        float target = 0;
        if (steering && CameraOrbit::ReadCameraRootWorldYaw(yaw) && std::isfinite(yaw)) {
            player->data.angle.z = Facing::Blend(player->data.angle.z, yaw, delta);
            g_fireBodyYaw = player->data.angle.z; // hip fire carries on from here
            if (standing) {
                // Legs planted; past the limit they are dragged round with the body.
                const float limit = TorsoTwist::Limit();
                target = std::clamp(Logic::SignedYaw(legsYaw - player->data.angle.z), -limit, limit);
                legsYaw = Logic::Normalize(player->data.angle.z + target);
            }
        }
        if (standing)
            legs = target;
        else
            legs = Twist::Approach(legs, 0, moving ? 14.0F : 6.0F, delta);
        if (std::abs(legs) < 0.005F)
            legs = 0;
        TorsoTwist::appliedLegs = legs;
    }

    void ResetGameplayState(bool keepLock = false) {
        ADSTurn::Cancel();
        DirectionalInput::Restore();
        CameraOrbit::Reset(keepLock);
        TorsoTwist::Reset();
        ThrowFacing::Reset();
        DirectionalHooks::steering.Reset();
        WaterWeaponVisibility::Reset();
        g_sprintPaused = false;
        DirectionalInput::settleUntil = 0;
        g_lastEquippedKnown = false;
    }

    // "Only when relaxed": a gun that is out and ready (drawing, alert, firing,
    // aiming) gets the game's own camera and movement; TTP takes over once the
    // gun relaxes. Switching works like the main switch, minus dropping a lock.
    bool ReadyGun(RE::PlayerCharacter *player) {
        const auto state = player->weaponState;
        return (state == RE::WEAPON_STATE::kWantToDraw || state == RE::WEAPON_STATE::kDrawing ||
                state == RE::WEAPON_STATE::kDrawn) &&
               player->gunState != RE::GUN_STATE::kRelaxed &&
               TargetLock::EquippedMode(player) == TargetLock::Mode::Ranged;
    }

    void UpdateReadySuspension(RE::PlayerCharacter *player) {
        const bool option = g_config.enabled && g_config.onlyWhenRelaxed && player;
        // Menus and VATS keep whatever was in effect; they have their own handover.
        if (option && (!Gameplay() || VATSMenuOpen()))
            return;
        const bool suspend = option && ReadyGun(player);
        if (suspend == g_readySuspended)
            return;
        if (suspend) {
            // The game's drawn camera sits behind the body, so turn the body to the
            // current view first; the picture stays where it was.
            float yaw = 0;
            auto *third = ActiveThirdPerson();
            if (third && !IronSights(third) && CameraOrbit::ReadCameraRootWorldYaw(yaw) &&
                std::isfinite(third->freeRotation.y)) {
                player->data.angle.z = Logic::Normalize(yaw);
                player->data.angle.x =
                    std::clamp(player->data.angle.x + third->freeRotation.y, -1.4F, 1.4F);
                third->freeRotation.y = 0;
            }
        }
        g_readySuspended = suspend;
        // Turning back on reseeds the orbit from the current view.
        ResetGameplayState(true);
        TTPDiagnostic::Trace(suspend ? "Gun ready: game camera" : "Gun relaxed: True Third Person");
    }

    void UpdateWorkbenchSuspension(bool entering = false) {
        if (entering || Workbench::Open()) {
            if (!Workbench::suspended) {
                Workbench::suspended = true;
                ResetGameplayState();
                EquipGuard::until = 0;
                TTPDiagnostic::Trace("Workbench: suspended");
            }
            Workbench::resumePending = false;
            Workbench::closedSince = 0;
            LockMarker::Hide();
            return;
        }
        if (!Workbench::suspended)
            return;
        const double now = Now();
        if (Workbench::closedSince == 0)
            Workbench::closedSince = now;
        if (now - Workbench::closedSince < Workbench::kCloseDebounceSeconds)
            return;
        Workbench::resumePending = true;
        // Leaving in first person: nothing to restore. Third person resumes from
        // the camera update hook once the view can be read.
        auto *camera = RE::PlayerCamera::GetSingleton();
        const auto *ui = RE::UI::GetSingleton();
        if (camera && ui && ui->menuMode == 0 &&
            camera->IsStateActive(RE::CameraStates::kFirstPerson)) {
            CameraOrbit::Reset();
            Workbench::suspended = false;
            Workbench::resumePending = false;
            Workbench::closedSince = 0;
            TTPDiagnostic::Trace("Workbench: resumed in first person");
        }
    }

    // How long the player's gun stays raised after a draw or a shot before it
    // drops to the relaxed pose: the game setting fGunPlayerRelaxedWaitTime.
    // The game's own value is kept so turning the option off puts it back.
    // A relaxed gun switches the game to its relaxed locomotion, which turns and
    // runs instead of strafing. While TTP holds the body (hip fire) or strafes it
    // back onto the keys (settle), the gun is kept ready with the game's own
    // longer time, so it can't relax in the middle of that.
    namespace RelaxTime {
        inline float gameValue = -1;
        inline float reported = -1;
        inline bool holding = false;
        inline void Apply() {
            auto *setting = RE::GetGameSetting("fGunPlayerRelaxedWaitTime");
            if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kFloat)
                return;
            if (gameValue < 0)
                gameValue = setting->GetFloat();
            const float chosen = g_config.customRelax ? g_config.relaxSeconds : gameValue;
            const float value = holding ? std::max(chosen, gameValue) : chosen;
            if (setting->GetFloat() != value)
                setting->SetFloat(value);
            if (chosen != reported) {
                reported = chosen;
                char line[128]{};
                std::snprintf(line, sizeof(line), "Relax time: %.2f s (game value %.2f s)", chosen,
                              gameValue);
                TTPDiagnostic::Trace(line);
            }
        }
        // Once per player update, before the native update.
        inline void Hold(bool hold) {
            if (hold == holding)
                return;
            holding = hold;
            Apply();
        }
    }

    // Settings are re-read when the pause menu closes (MCM lives there).
    std::atomic_bool g_settingsReloadPending{false};

    void ApplyPendingSettings() {
        if (!Gameplay() || VATSMenuOpen() || !g_settingsReloadPending.exchange(false))
            return;
        McmReset::Run();
        const auto previous = g_config;
        LoadConfig();
        const auto next = g_config;
        g_config = previous;
        // Release with the current settings still active so the camera hands back cleanly.
        if (TargetLock::lockRequested && !TargetLock::SettingsFor(TargetLock::lockedMode).enabled)
            TargetLock::Unlock();
        // Hooks are always installed and step aside while disabled, so the main
        // switch applies immediately. Drop all camera/movement state on a change;
        // turning on reseeds the orbit from the current view.
        if (previous.enabled != next.enabled)
            ResetGameplayState();
        g_config = next;
        RelaxTime::Apply();
        if (previous.holsteredOffset != next.holsteredOffset ||
            previous.rangedOffset != next.rangedOffset || previous.meleeOffset != next.meleeOffset)
            CameraOrbit::RefreshShoulderOffset();
        TTPDiagnostic::Trace(previous.enabled == next.enabled ? "Settings reloaded"
                             : next.enabled ? "Settings reloaded: True Third Person on"
                                            : "Settings reloaded: True Third Person off");
    }

    struct PlayerUpdateHook {
        static void Thunk(RE::PlayerCharacter *self, float delta) {
            Profile::EndFrame();
            Profile::Scope timing;
            UpdateWorkbenchSuspension();
            ApplyPendingSettings();
            UpdateReadySuspension(self);
            LockMarker::Ensure();
            if (CameraOrbit::YieldToVATS()) {
                DirectionalInput::Restore();
                LockMarker::Hide();
                Profile::Call(original, self, delta);
                WaterWeaponVisibility::Update(self);
                return;
            }
            if (Workbench::suspended) {
                Profile::Call(original, self, delta);
                return;
            }
            CameraOrbit::Install();
            // A favorite can finish while the camera callbacks skipped the check.
            if (!g_cameraWeaponSpoofActive && !FavoriteSwitchInProgress(self))
                g_favoriteCameraHeld = false;
            PowerArmorCamera::Install();
            CompassHeadingHook::Install();
            WatchEquipChanges(self);
            ADSStateHook::Tick(self, delta);
            TargetLock::Tick(self, delta);
            CameraOrbit::ObserveHolster();
            CameraOrbit::RefreshHipFire(self);
            CameraOrbit::FaceHipFire(self, delta);
            if (Gameplay())
                CameraOrbit::SyncAimTransition();

            SuppressSprintForMeleeSwitch(self);
            DirectionalInput::LearnDrawCorrection(self);
            DirectionalInput::Prepare(self);
            RelaxTime::Hold(HipFireMovementActive(self) || DirectionalInput::settling);
            DrawTrace::Sample(self);
            Profile::Call(original, self, delta); // real weapon state reaches native equip/animation code
            DirectionalInput::Restore();
            if (ADSTurn::pending)
                self->data.angle.z = ADSTurn::yaw;
            WaterWeaponVisibility::Update(self);
            TargetLock::RestoreFacing(self);
            CameraOrbit::FaceHipFire(self);
            FaceThrow(self, delta);
            ThrowAim::Tick(self);
            CameraOrbit::BlendMovementReturn(self, delta);
            {
                float cameraYaw = std::numeric_limits<float>::quiet_NaN();
                auto *third = ActiveThirdPerson();
                if (third && !IronSights(third))
                    CameraOrbit::ReadCameraRootWorldYaw(cameraYaw);
                TorsoTwist::Update(HipFireWithTwist(self) || ThrowFacing::twisting, cameraYaw,
                                   self->data.angle.z, delta);
            }
            LockMarker::Track();
        }

        static bool Install() {
            static bool installed = false;
            if (!installed) {
                auto *player = RE::PlayerCharacter::GetSingleton();
                if (!player)
                    return false;
                auto hooks = HookBatch::For(player);
                hooks.Add(0xCF, Thunk, original);
                installed = hooks.Commit();
            }
            return installed;
        }

        static inline decltype(&Thunk) original = nullptr;
    };

    // Stops the idle "turn to camera" rotation while standing still with a weapon out.
    struct PlayerControlsOutputHook {
        static void Thunk(RE::IMovementPlayerControls *self, std::uint32_t numericID,
                          RE::PlayerControlsMovementData &output) {
            Profile::Scope timing;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (Gameplay())
                CameraOrbit::SyncAimTransition();

            SuppressSprintForMeleeSwitch(player);
            DirectionalInput::Prepare(player);
            Profile::Call(original, self, numericID, output);
            auto *controls = RE::PlayerControls::GetSingleton();
            const auto state = player ? player->weaponState : RE::WEAPON_STATE::kSheathed;
            if (!player || !controls || !IndependentLocomotionActive(player) ||
                (state != RE::WEAPON_STATE::kDrawn && state != RE::WEAPON_STATE::kWantToDraw &&
                 state != RE::WEAPON_STATE::kDrawing) ||
                EquipGuard::Active() ||
                player->DoGetCharacterState() != RE::IMovementState::CHARACTER_STATE::kOnGround)
                return;
            const auto input =
                DirectionalInput::valid ? DirectionalInput::raw : controls->data.moveInputVec;
            if (Facing::Stationary(input.x, input.y, output.movementSpeed))
                Facing::ClearCameraTurn(output, player->data.angle.z);
        }

        static bool Install() {
            if (g_controlsHookInstalled.load(std::memory_order_acquire))
                return true;
            auto *controls = RE::PlayerControls::GetSingleton();
            if (!controls)
                return false;
            auto hooks = HookBatch::For(static_cast<RE::IMovementPlayerControls *>(controls));
            hooks.Add(0x01, Thunk, original);
            if (!hooks.Commit())
                return false;
            g_controlsHookInstalled.store(true, std::memory_order_release);
            return true;
        }

        static inline decltype(&Thunk) original = nullptr;
    };

    struct MenuObserver final : RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent &event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override {
            if (event.menuName == "PauseMenu") {
                McmReset::pauseMenuOpen = event.opening;
                if (!event.opening)
                    g_settingsReloadPending.store(true);
            }
            if (event.opening && LockMarker::ConflictsWith(event.menuName))
                LockMarker::Hide();
            bool workbenchMenu = false;
            for (const auto *name : Workbench::menus)
                workbenchMenu = workbenchMenu || event.menuName == name;
            if (workbenchMenu || Workbench::suspended) {
                char line[160]{};
                std::snprintf(line, sizeof(line), "Menu %s: %s (marker %s)",
                              event.opening ? "open" : "close", event.menuName.c_str(),
                              MenuOpen(LockMarker::menuName) ? "open" : "closed");
                TTPDiagnostic::Trace(line);
            }
            UpdateWorkbenchSuspension(event.opening && workbenchMenu);
            if (event.menuName == "LoadingMenu")
                CameraOrbit::LoadingChanged(event.opening);
            return RE::BSEventNotifyControl::kContinue;
        }
        static void Install() {
            static MenuObserver observer;
            static bool installed = false;
            auto *ui = RE::UI::GetSingleton();
            if (!installed && ui)
                installed =
                    static_cast<RE::BSTEventSource<RE::MenuOpenCloseEvent> *>(ui)->RegisterSink(
                        &observer);
        }
    };

    void Report(bool ok, const char *success, const char *failure) {
        TTPDiagnostic::Trace(ok ? success : failure);
    }

    void InstallHooks() {
        MenuObserver::Install();
        Report(CameraOrbit::Install(), "Camera hooks installed", "Camera hooks FAILED");
        Report(PlayerControlsOutputHook::Install(), "Movement output hook installed",
               "Movement output hook FAILED");
        Report(DirectionalHooks::Install(), "Directional movement hooks installed",
               "Directional movement hooks FAILED");
        Report(TorsoTwist::AnimationHook::Install(), "Torso twist hook installed",
               "Torso twist hook FAILED");
        Report(PlayerUpdateHook::Install(), "Player update hook installed",
               "Player update hook FAILED");
        Report(ADSStateHook::Install(), "ADS hook installed", "ADS hook FAILED");
        Report(PowerArmorCamera::Install(), "Power armor camera patched",
               "Power armor camera not patched");
        Report(CompassHeadingHook::Install(), "Compass heading patched",
               "Compass heading not patched");
        LockMarker::Ensure();
        EquipGuard::onButton = [](const RE::ButtonEvent *button) {
            CameraOrbit::HipFireButton(button);
            ThrowFacing::Button(button);
        };
        EquipGuard::onFavorite = CameraOrbit::BeginFavoriteCamera;
        Report(EquipGuard::InputObserver::Install(), "Input observer installed",
               "Input observer FAILED");
        TargetLock::onRelease = CameraOrbit::BeginLockRelease;
        EquipGuard::filterInput = TargetLock::Input;
        TargetLock::ready =
            EquipGuard::InputObserver::original && TargetLock::CameraInputHook::Install();
        Report(TargetLock::ready, "Target lock ready", "Target lock disabled: input hook FAILED");
    }

    void MessageHandler(F4SE::MessagingInterface::Message *message) {
        if (!message)
            return;
        using Type = F4SE::MessagingInterface::MessageType;
        switch (message->GetType()) {
        case Type::kPreLoadGame:
            Workbench::suspended = Workbench::resumePending = false;
            Workbench::closedSince = 0;
            g_gameReady = false;
            ResetGameplayState();
            return;
        case Type::kPostLoadGame:
        case Type::kNewGame:
            ResetGameplayState();
            RelaxTime::Apply();
            g_gameReady = true;
            break;
        case Type::kGameDataReady:
            RelaxTime::Apply();
            break;
        default:
            return;
        }
        // Installed even when disabled: every hook passes straight through then,
        // which lets the main switch and lock-only mode change without a restart.
        InstallHooks();
    }
}

F4SE_PLUGIN_VERSION = []() consteval noexcept {
    F4SE::PluginVersionData info;
    info.SetPluginName("TrueThirdPerson");
    info.SetPluginVersion(TrueThirdPerson::kPluginVersion);
    info.SetUseSignatureScanning(true); // addresses are built in; no Address Library
    info.SetCompatibleVersions(TrueThirdPerson::kTargetRuntimes);
    return info;
}();

// F4SE 0.6.x still calls Query instead of reading the version data above.
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface *f4se, F4SE::PluginInfo *info) {
    if (!f4se || !info)
        return false;
    info->SetDataVersion(F4SE::PluginInfo::DATA_VERSION);
    info->SetPluginName("TrueThirdPerson");
    info->SetPluginVersion(TrueThirdPerson::kPluginVersion);
    return !f4se->IsEditor() && TrueThirdPerson::SupportedRuntime(f4se->GetRuntimeVersion());
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface *f4se) {
    TTPDiagnostic::BeginSession("=== TrueThirdPerson 0.2.5 ===");
    if (!f4se || f4se->IsEditor() ||
        !TrueThirdPerson::SupportedRuntime(f4se->GetRuntimeVersion())) {
        TTPDiagnostic::Trace("Not loaded: editor or unsupported game version");
        return false;
    }
    try {
        F4SE::Init(f4se, {.logName = "TrueThirdPerson", .logLevel = REX::LogLevel::kInformation});
        TrueThirdPerson::LoadConfig();
        F4SE::GetPapyrusInterface()->Register(TrueThirdPerson::McmReset::BindVM);
        TrueThirdPerson::McmReset::Watch(TrueThirdPerson::GameRoot() / L"Data");
        const bool registered = F4SE::GetMessagingInterface()->RegisterListener(
            TrueThirdPerson::MessageHandler, "F4SE");
        TTPDiagnostic::Trace(registered ? "Loaded" : "Not loaded: message listener FAILED");
        return registered;
    } catch (const std::exception &error) {
        TTPDiagnostic::Trace("Not loaded: exception during initialization");
        TTPDiagnostic::Trace(error.what());
        return false;
    } catch (...) {
        TTPDiagnostic::Trace("Not loaded: unknown exception during initialization");
        return false;
    }
}
