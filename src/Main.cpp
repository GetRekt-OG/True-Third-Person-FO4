#include "PCH.hpp"
#include "StartupLog.hpp"
#include "CameraSupport.hpp"
#include "EquipGuard.hpp"
#include "FacingBlend.hpp"
#include "DirectionalMovement.hpp"
#include "MeleeLock.hpp"
#include "WaterWeaponVisibility.hpp"
#include "IdleRotation.hpp"
#include "ADSTurn.hpp"
#include "LockMarker.hpp"
#include "RE/B/BipedAnim.hpp"
#include "RE/C/Console.hpp"
#include "RE/H/HUDMenu.hpp"
#include "RE/P/PauseMenu.hpp"

#include <cmath>
#include <cstring>
#include "REL/Trampoline.hpp"
#include "PowerArmorCamera.hpp"

namespace TrueThirdPerson {
    constexpr std::array kTargetRuntimes{REL::CreateRuntime(1, 10, 163, 0),
                                         REL::CreateRuntime(1, 11, 221, 0),
                                         REL::CreateRuntime(1, 11, 240, 0)};

    constexpr std::size_t kPlayerUpdateIndex = 0xCF;
    constexpr std::size_t kGetControllerOutputIndex = 0x01;

    constexpr std::size_t kCameraStateBeginIndex = 0x09;
    constexpr std::size_t kCameraStateUpdateIndex = 0x0B;
    constexpr std::size_t kProcessWeaponDrawnChangeIndex = 0x11;
    constexpr std::size_t kSetFreeRotationModeIndex = 0x13;
    constexpr std::size_t kUpdateRotationIndex = 0x14;
    constexpr std::size_t kHandleLookInputIndex = 0x15; // Commonwealth Camera-compatible chained look-input hook.
    constexpr std::uintptr_t kTPFreeRotationOffset = offsetof(RE::ThirdPersonState, freeRotation);
    constexpr std::uintptr_t kTPTargetYawOffset = offsetof(RE::ThirdPersonState, targetYaw);
    constexpr std::uintptr_t kTPCurrentYawOffset = offsetof(RE::ThirdPersonState, currentYaw);
    constexpr std::uintptr_t kTPFreeRotationEnabledOffset =
        offsetof(RE::ThirdPersonState, freeRotationEnabled);
    constexpr std::uintptr_t kTPIronSightsOffset = offsetof(RE::ThirdPersonState, ironSights);
    constexpr std::uintptr_t kPlayerCameraFreeRotationReadyOffset =
        offsetof(RE::PlayerCamera, freeRotationReady);

    std::atomic_bool g_directionalHooksInstalled{false};
    std::atomic_bool g_controlsHookInstalled{false};
    std::atomic_bool g_cameraHooksInstalled{false};
    thread_local bool g_cameraWeaponSpoofActive = false;
    thread_local bool g_locomotionSpoofActive = false;
    thread_local bool g_locomotionRealWeaponDrawn = false;

    [[nodiscard]] bool IndependentFacingActive(const RE::PlayerCharacter *a_player) {
        if (!g_config.enabled || !Gameplay() || !a_player || !WeaponDrawn(a_player)) {
            return false;
        }

        const auto *filter = static_cast<const RE::IMovementPlayerControlsFilter *>(a_player);
        return filter && filter->IsInThirdPerson();
    }

    struct CameraAccess {
        struct Point2 {
            float x;
            float y;
        };

        [[nodiscard]] static void *GetPlayerCamera() {
            return RE::PlayerCamera::GetSingleton();
        }

        [[nodiscard]] static void *GetCurrentState(void *a_camera) {
            if (!a_camera) {
                return nullptr;
            }
            return static_cast<RE::PlayerCamera *>(a_camera)->GetState().get();
        }

        [[nodiscard]] static void *GetThirdPersonState(void *a_camera) {
            if (!a_camera) {
                return nullptr;
            }
            return static_cast<RE::PlayerCamera *>(a_camera)
                ->GetState(RE::CameraStates::kThirdPerson)
                .get();
        }

        [[nodiscard]] static bool IsIronSights(void *a_state) {
            return a_state && *reinterpret_cast<bool *>(reinterpret_cast<std::uintptr_t>(a_state) +
                                                        kTPIronSightsOffset);
        }
    };

    [[nodiscard]] bool IronSightsActive() {
        void *camera = CameraAccess::GetPlayerCamera();
        void *thirdPerson = CameraAccess::GetThirdPersonState(camera);
        return camera && thirdPerson && CameraAccess::GetCurrentState(camera) == thirdPerson &&
               CameraAccess::IsIronSights(thirdPerson);
    }

    thread_local bool g_hipFireCameraScoped = false;
    double g_hipFireUntil = 0;
    bool g_hipFireWasActive = false;
    unsigned g_hipFireReleaseFrames = 0;
    bool g_fireBodyValid = false;
    float g_fireBodyYaw = 0, g_returnYaw = 0, g_returnRemaining = 0;

    bool HipFireContext(const RE::PlayerCharacter *player) {
        if (!g_config.hipFireFacing || !IndependentFacingActive(player) ||
            player->weaponState != RE::WEAPON_STATE::kDrawn || IronSightsActive() ||
            EquipGuard::Now() < EquipGuard::until || !player->biped)
            return false;
        const auto *gun = player->biped->GetBipObject(RE::BIPED_OBJECT::kWeaponGun);
        return gun && gun->parent.object && gun->partClone;
    }

    bool HipFireMovementActive(const RE::PlayerCharacter *player) {
        return HipFireContext(player) && EquipGuard::Now() < g_hipFireUntil;
    }

    bool g_favoriteCameraHeld = false;

    bool FavoriteSwitchInProgress(const RE::PlayerCharacter *player) {
        if (!g_favoriteCameraHeld || !player)
            return false;
        // Camera callbacks temporarily report sheathed. Do not use that scoped
        // state as evidence that the actual equip animation has completed.
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
            return EquipGuard::Now() < EquipGuard::until;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IndependentLocomotionActive(RE::PlayerCharacter *a_player) {
        // Use the active camera to determine movement eligibility.
        const auto *camera = RE::PlayerCamera::GetSingleton();
        return g_config.enabled && Gameplay() && camera &&
               camera->IsStateActive(RE::CameraStates::kThirdPerson) &&
               g_cameraHooksInstalled.load() && g_directionalHooksInstalled.load() &&
               g_controlsHookInstalled.load() && !HipFireMovementActive(a_player) &&
               !MeleeLock::Active() && a_player &&
               a_player->lifeState == RE::ACTOR_LIFE_STATE::kAlive && !a_player->swimming &&
               a_player->DoGetCharacterState() != RE::IMovementState::CHARACTER_STATE::kSwimming &&
               (a_player->weaponState == RE::WEAPON_STATE::kDrawn ||
                a_player->weaponState == RE::WEAPON_STATE::kWantToSheathe ||
                a_player->weaponState == RE::WEAPON_STATE::kSheathing ||
                FavoriteSwitchInProgress(a_player)) &&
               !IronSightsActive();
    }

    struct ThirdPersonCameraOrbitHook {
        using Point2 = CameraAccess::Point2;

        static bool ConsoleOpen() {
            const auto *ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen<RE::Console>().value_or(false);
        }

        static bool SettingsOpen() {
            const auto *ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen<RE::PauseMenu>().value_or(false);
        }

        static bool GameWindowFocused() {
            DWORD process = 0;
            const auto foreground = GetForegroundWindow();
            return foreground && GetWindowThreadProcessId(foreground, &process) &&
                   process == GetCurrentProcessId();
        }

        static bool CameraContextAvailable() {
            // Focus gates input, not ownership of the existing camera orbit.
            const auto *ui = RE::UI::GetSingleton();
            return g_gameReady && !g_loading && ui &&
                   (ui->menuMode == 0 || ConsoleOpen() || SettingsOpen());
        }

        [[nodiscard]] static float NormalizeAngle(float a_angle) {
            return Logic::Normalize(a_angle);
        }

        [[nodiscard]] static float NormalizeRelativeAngle(float a_angle) {
            return Logic::SignedYaw(a_angle);
        }

        [[nodiscard]] static bool ReadNodeWorldYaw(const RE::NiAVObject *a_node, float &a_yaw) {
            if (!a_node) {
                return false;
            }

            const auto &matrix = a_node->world.rotation;
            const float x = matrix.rows[0].x, y = matrix.rows[1].x;
            if (!std::isfinite(x) || !std::isfinite(y) || std::hypot(x, y) < 0.00001F)
                return false;
            a_yaw = NormalizeAngle(std::atan2(y, x));
            return true;
        }

        [[nodiscard]] static bool ReadCameraRootWorldYaw(float &a_yaw) {
            void *camera = CameraAccess::GetPlayerCamera();
            if (!camera) {
                return false;
            }

            return ReadNodeWorldYaw(static_cast<RE::PlayerCamera *>(camera)->cameraRoot.get(),
                                    a_yaw);
        }

        static void ApplyTransitionReseed(void *a_state) {
            if (!a_state || g_transitionReseedFrames == 0) {
                return;
            }

            const auto stateBase = reinterpret_cast<std::uintptr_t>(a_state);
            auto *freeRotation = reinterpret_cast<Point2 *>(stateBase + kTPFreeRotationOffset);
            auto *targetYaw = reinterpret_cast<float *>(stateBase + kTPTargetYawOffset);
            auto *currentYaw = reinterpret_cast<float *>(stateBase + kTPCurrentYawOffset);

            *targetYaw = g_transitionAnchorYaw;
            *currentYaw = g_transitionAnchorYaw;
            *freeRotation = g_transitionFreeRotation;
            g_savedFreeRotation = g_transitionFreeRotation;
            g_savedFreeRotationValid = true;
        }

        static void StartTransitionReseed(void *a_state, float a_anchorYaw, Point2 a_freeRotation) {
            g_transitionAnchorYaw = NormalizeAngle(a_anchorYaw);
            g_transitionFreeRotation = a_freeRotation;
            g_transitionReseedFrames = 2;
            ApplyTransitionReseed(a_state);
        }

        static void RememberCameraRootWorldYaw() {
            float cameraYaw = 0.0F;
            if (ReadCameraRootWorldYaw(cameraYaw)) {
                g_lastCameraRootWorldYaw = cameraYaw;
                g_lastCameraRootWorldYawValid = true;
            }
        }

        static void RememberADSCameraRootWorldYaw() {
            float cameraYaw = 0.0F;
            if (ReadCameraRootWorldYaw(cameraYaw)) {
                g_lastADSCameraRootWorldYaw = cameraYaw;
                g_lastADSCameraRootWorldYawValid = true;
            }
        }

        static void ResetHipFire() {
            g_fireBodyValid = false;
            g_returnRemaining = 0;
            g_hipFireUntil = 0;
            g_hipFireWasActive = false;
            g_hipFireReleaseFrames = 0;
        }

        static void RefreshHipFire(RE::PlayerCharacter *player) {
            if (g_cameraWeaponSpoofActive || g_locomotionSpoofActive)
                return;
            if (!HipFireContext(player)) {
                ResetHipFire();
                return;
            }
            if (player->gunState == RE::GUN_STATE::kFire)
                g_hipFireUntil = EquipGuard::Now() + g_config.hipFireHoldMs / 1000.0;
            const bool firing = HipFireMovementActive(player);
            if (firing) {
                g_hipFireReleaseFrames = 0;
                g_returnRemaining = 0;
            } else if (g_hipFireWasActive) {
                g_hipFireReleaseFrames = 2;
                g_returnYaw = player->data.angle.z;
                g_returnRemaining = g_config.smoothHipFire ? 0.18F : 0;
                g_fireBodyValid = false;
                TTPDiagnostic::Trace("Hip fire: returning to base movement");
            }
            g_hipFireWasActive = firing;
        }

        static bool HipFireCameraActive(void *state) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = CameraAccess::GetPlayerCamera();
            return state && CameraAccess::GetCurrentState(camera) == state &&
                   CameraAccess::GetThirdPersonState(camera) == state &&
                   (g_hipFireCameraScoped || HipFireContext(player)) &&
                   (EquipGuard::Now() < g_hipFireUntil || g_hipFireReleaseFrames > 0 ||
                    g_returnRemaining > 0);
        }

        static void FaceHipFire(RE::PlayerCharacter *player, float delta = 0) {
            // Power armor turns through the movement hook so native locomotion
            // can apply its animation/root motion without a forced 3D refresh.
            if (player && InPowerArmor(player) && g_directionalHooksInstalled.load())
                return;
            if (!HipFireMovementActive(player))
                return;
            auto *camera = CameraAccess::GetPlayerCamera();
            auto *third =
                static_cast<RE::ThirdPersonState *>(CameraAccess::GetThirdPersonState(camera));
            if (!third || CameraAccess::GetCurrentState(camera) != third)
                return;
            // Keep the absolute orbit yaw when the firing stance ends.
            const float yaw =
                g_savedFreeRotationValid ? g_savedFreeRotation.x : third->freeRotation.x;
            if (!std::isfinite(yaw))
                return;
            if (!g_fireBodyValid) {
                g_fireBodyYaw = player->data.angle.z;
                g_fireBodyValid = true;
            }
            g_fireBodyYaw = g_config.smoothHipFire ? FacingBlend::Step(g_fireBodyYaw, yaw, delta)
                                                   : NormalizeAngle(yaw);
            player->data.angle.z = g_fireBodyYaw;
            player->Update3DPosition(false);
        }

        static void BlendMovementReturn(RE::PlayerCharacter *player, float delta) {
            if (player && InPowerArmor(player) && g_directionalHooksInstalled.load()) {
                g_returnRemaining = 0;
                return;
            }
            if (!player || g_returnRemaining <= 0 || !HipFireContext(player))
                return;
            const float dt = std::isfinite(delta) ? std::clamp(delta, 0.0F, 0.05F) : 0;
            // Follow the direction vanilla just selected; never restore the old
            // pre-shot facing or change camera angles.
            g_returnYaw =
                FacingBlend::Step(g_returnYaw, player->data.angle.z, dt, g_returnRemaining);
            g_returnRemaining = std::max(0.0F, g_returnRemaining - dt);
            player->data.angle.z = g_returnYaw;
            player->Update3DPosition(false);
        }

        static void HipFireButton(const RE::ButtonEvent *button) {
            if (!button->QJustPressed())
                return;
            const std::string_view event = button->QUserEvent().c_str();
            if (event != "PrimaryAttack" && event != "Attack")
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!HipFireContext(player))
                return;
            // Start facing before forwarding attack input. Native gun state keeps
            // the facing window open during sustained fire.
            if (!HipFireMovementActive(player)) {
                g_fireBodyYaw = player->data.angle.z;
                g_fireBodyValid = true;
            }
            g_returnRemaining = 0;
            g_hipFireUntil = EquipGuard::Now() + g_config.hipFireHoldMs / 1000.0;
            g_hipFireReleaseFrames = 0;
            g_hipFireWasActive = true;
            FaceHipFire(player);
            TTPDiagnostic::Trace("Hip fire: attack facing started");
        }

        static void BeginFavoriteCamera() {
            ADSTurn::Cancel();
            MeleeLock::Unlock();
            ResetHipFire();
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = CameraAccess::GetPlayerCamera();
            auto *third = CameraAccess::GetThirdPersonState(camera);
            g_favoriteCameraHeld = g_config.enabled && Gameplay() && player &&
                                   (WeaponDrawn(player) || g_favoriteCameraHeld) && third &&
                                   CameraAccess::GetCurrentState(camera) == third &&
                                   !CameraAccess::IsIronSights(third);
            if (g_favoriteCameraHeld)
                TTPDiagnostic::Trace("Favorites: camera-only weapon-state masking armed");
        }

        static bool FavoriteCameraActive(void *state) {
            if (!g_favoriteCameraHeld)
                return false;
            auto *camera = CameraAccess::GetPlayerCamera();
            if (!FavoriteSwitchInProgress(RE::PlayerCharacter::GetSingleton()) ||
                !g_config.enabled || !CameraContextAvailable() || !state ||
                CameraAccess::GetCurrentState(camera) != state ||
                CameraAccess::GetThirdPersonState(camera) != state ||
                CameraAccess::IsIronSights(state)) {
                g_favoriteCameraHeld = false;
                return false;
            }
            return true;
        }

        static inline bool g_holsterCameraHeld = false;
        static inline bool g_holsteredOrbit = false;
        static inline bool g_drawOrbit = false;
        static inline bool g_lastRealDrawn = false;
        static inline unsigned g_holsterReleaseFrames = 0;

        // Called before the player update, outside both kinds of temporary spoof.
        static void ObserveHolster() {
            if (g_locomotionSpoofActive || g_cameraWeaponSpoofActive)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = CameraAccess::GetPlayerCamera();
            auto *third = CameraAccess::GetThirdPersonState(camera);
            if (!g_config.enabled || !CameraContextAvailable() || !player || !third ||
                CameraAccess::GetCurrentState(camera) != third ||
                CameraAccess::IsIronSights(third)) {
                g_holsterCameraHeld = false;
                g_holsteredOrbit = false;
                g_drawOrbit = false;
                g_lastRealDrawn = false;
                g_holsterReleaseFrames = 0;
                return;
            }
            const auto state = player->weaponState;
            const bool sheathing =
                state == RE::WEAPON_STATE::kWantToSheathe || state == RE::WEAPON_STATE::kSheathing;
            if (g_holsteredOrbit && g_savedFreeRotationValid &&
                (state == RE::WEAPON_STATE::kWantToDraw || state == RE::WEAPON_STATE::kDrawing ||
                 state == RE::WEAPON_STATE::kDrawn)) {
                g_drawOrbit = true;
            }
            const bool directHolster = g_lastRealDrawn && state == RE::WEAPON_STATE::kSheathed;
            // Favorites already own their camera transition; leave that path alone.
            if (!g_holsterCameraHeld && !FavoriteCameraActive(third) && g_savedFreeRotationValid &&
                (sheathing || directHolster)) {
                g_holsterCameraHeld = true;
                g_holsterReleaseFrames = 2;
                TTPDiagnostic::Trace("Holster: camera-only protection started");
            }
            if (state != RE::WEAPON_STATE::kSheathed && !sheathing) {
                g_holsterCameraHeld = false;
                g_holsteredOrbit = false;
                g_holsterReleaseFrames = 0;
            }
            g_lastRealDrawn = state == RE::WEAPON_STATE::kDrawn;
        }

        static bool HolsterCameraActive(void *state) {
            if (!g_holsterCameraHeld && !g_holsteredOrbit && !g_drawOrbit)
                return false;
            auto *camera = CameraAccess::GetPlayerCamera();
            if (!g_config.enabled || !CameraContextAvailable() || !state ||
                CameraAccess::GetCurrentState(camera) != state ||
                CameraAccess::GetThirdPersonState(camera) != state ||
                CameraAccess::IsIronSights(state)) {
                g_holsterCameraHeld = false;
                g_holsteredOrbit = false;
                g_drawOrbit = false;
                g_holsterReleaseFrames = 0;
                return false;
            }
            return true;
        }

        static void FinishHolsterCameraUpdate() {
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (g_holsterCameraHeld && !g_locomotionSpoofActive && !g_cameraWeaponSpoofActive &&
                player && player->weaponState == RE::WEAPON_STATE::kSheathed &&
                g_holsterReleaseFrames > 0) {
                if (--g_holsterReleaseFrames == 0) {
                    g_holsterCameraHeld = false;
                    g_holsteredOrbit = g_config.keepHolsterView;
                    TTPDiagnostic::Trace(g_holsteredOrbit ? "Holster: keeping current orbit"
                                                          : "Holster: camera protection released");
                }
            }
        }

        // Apply the same camera isolation before and after a holster/draw cycle.
        // Derive it from the live context so POV and menu transitions cannot
        // leave an armed camera waiting for g_drawOrbit to be set.
        static bool ArmedCameraActive(void *state) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = CameraAccess::GetPlayerCamera();
            return g_config.enabled && CameraContextAvailable() && player && camera && state &&
                   CameraAccess::GetCurrentState(camera) == state &&
                   CameraAccess::GetThirdPersonState(camera) == state &&
                   !CameraAccess::IsIronSights(state) &&
                   (WeaponDrawn(player) || g_cameraWeaponSpoofActive);
        }

        struct ScopedFavoriteCameraWeapon {
            RE::PlayerCharacter *player = nullptr;
            RE::WEAPON_STATE previousState{};
            bool previousSpoof = g_cameraWeaponSpoofActive;
            bool previousHipFireScope = g_hipFireCameraScoped;
            bool previousLockScope = MeleeLock::cameraScope;
            bool previousPowerArmorScope = PowerArmorCamera::scoped;
            explicit ScopedFavoriteCameraWeapon(void *state) {
                if (!ProtectedOrbit(state))
                    return;
                player = RE::PlayerCharacter::GetSingleton();
                if (!player)
                    return;
                if (InPowerArmor(player)) {
                    PowerArmorCamera::scoped = PowerArmorCamera::installed;
                    player = nullptr; // retain the real weapon state in power armor
                    return;
                }
                g_hipFireCameraScoped = HipFireCameraActive(state);
                MeleeLock::cameraScope = MeleeLock::ProtectCamera();
                previousState = player->weaponState;
                g_cameraWeaponSpoofActive = true;
                player->weaponState = RE::WEAPON_STATE::kSheathed;
            }
            ~ScopedFavoriteCameraWeapon() {
                PowerArmorCamera::scoped = previousPowerArmorScope;
                if (!player)
                    return;
                if (player->weaponState == RE::WEAPON_STATE::kSheathed)
                    player->weaponState = previousState;
                g_cameraWeaponSpoofActive = previousSpoof;
                g_hipFireCameraScoped = previousHipFireScope;
                MeleeLock::cameraScope = previousLockScope;
            }
        };

        [[nodiscard]] static bool Active(void *a_state) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            void *camera = CameraAccess::GetPlayerCamera();
            const auto *filter =
                player ? static_cast<const RE::IMovementPlayerControlsFilter *>(player) : nullptr;
            const bool activeMode = g_locomotionSpoofActive ? (g_locomotionRealWeaponDrawn &&
                                                               filter && filter->IsInThirdPerson())
                                                            : IndependentFacingActive(player);

            const bool transitionMode =
                CameraContextAvailable() && g_config.enabled && g_transitionReseedFrames > 0 &&
                player && (WeaponDrawn(player) || g_holsteredOrbit) && camera && a_state &&
                CameraAccess::GetThirdPersonState(camera) == a_state;
            const bool currentStateActive =
                (activeMode || ArmedCameraActive(a_state) || FavoriteCameraActive(a_state) ||
                 HolsterCameraActive(a_state) || HipFireCameraActive(a_state) ||
                 MeleeLock::ProtectCamera()) &&
                camera && a_state && CameraAccess::GetCurrentState(camera) == a_state;

            return CameraContextAvailable() && g_config.enabled &&
                   (currentStateActive || transitionMode) && !CameraAccess::IsIronSights(a_state);
        }

        static void SyncAimTransition() {
            ResumeAfterLoading();
            if (g_loading)
                return;
            if (g_cameraWeaponSpoofActive)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            void *camera = CameraAccess::GetPlayerCamera();
            void *thirdPerson = CameraAccess::GetThirdPersonState(camera);
            const bool inThirdPerson =
                camera && thirdPerson && CameraAccess::GetCurrentState(camera) == thirdPerson;

            if (camera && static_cast<RE::PlayerCamera *>(camera)->IsStateActive(
                              RE::CameraStates::kFirstPerson)) {
                g_lastFirstPersonPitch = player ? player->data.angle.x : 0.0F;
                float firstPersonCameraYaw = 0.0F;
                if (ReadCameraRootWorldYaw(firstPersonCameraYaw)) {
                    g_lastFirstPersonCameraYaw = firstPersonCameraYaw;
                    g_lastFirstPersonCameraYawValid = true;
                }
            } else if (!inThirdPerson) {
                g_lastFirstPersonCameraYawValid = false;
            }

            if (inThirdPerson != g_wasThirdPerson) {
                if (inThirdPerson && camera && thirdPerson && player &&
                    (WeaponDrawn(player) || (g_config.keepHolsterView &&
                                             player->weaponState == RE::WEAPON_STATE::kSheathed))) {
                    float cameraWorldYaw = 0.0F;
                    const bool usedFirstPersonYaw = g_lastFirstPersonCameraYawValid;
                    if (usedFirstPersonYaw) {
                        cameraWorldYaw = g_lastFirstPersonCameraYaw;
                    } else if (!ReadCameraRootWorldYaw(cameraWorldYaw)) {
                        cameraWorldYaw = player->data.angle.z;
                    }

                    const Point2 desiredFreeRotation{
                        NormalizeRelativeAngle(cameraWorldYaw),
                        usedFirstPersonYaw ? g_lastFirstPersonPitch - player->data.angle.x : 0.0F};
                    if (player->weaponState == RE::WEAPON_STATE::kSheathed &&
                        g_config.keepHolsterView)
                        g_holsteredOrbit = true;

                    if (!g_seededOnThirdPersonBegin) {
                        StartTransitionReseed(thirdPerson, player->data.angle.z,
                                              desiredFreeRotation);
                    }
                    g_seededOnThirdPersonBegin = false;
                } else {
                    g_savedFreeRotationValid = false;
                    g_transitionReseedFrames = 0;
                    g_seededOnThirdPersonBegin = false;
                    g_lastCameraRootWorldYawValid = false;
                    g_lastADSCameraRootWorldYawValid = false;
                }
                g_wasThirdPerson = inThirdPerson;
            }

            const bool aiming = player && WeaponDrawn(player) && camera && thirdPerson &&
                                CameraAccess::GetCurrentState(camera) == thirdPerson &&
                                CameraAccess::IsIronSights(thirdPerson);

            // A native camera notification can precede the actor-state handoff.
            // Do not apply the immediate facing snap during our pre-turn.
            if (aiming && ADSTurn::pending)
                return;
            if (aiming == g_wasAiming) {
                if (aiming) {
                    RememberADSCameraRootWorldYaw();
                }
                return;
            }

            if (aiming) {
                g_transitionReseedFrames = 0;
                float cameraWorldYaw = 0.0F;
                const bool haveCameraYaw =
                    g_lastCameraRootWorldYawValid || ReadCameraRootWorldYaw(cameraWorldYaw);
                if (g_lastCameraRootWorldYawValid) {
                    cameraWorldYaw = g_lastCameraRootWorldYaw;
                }

                if (haveCameraYaw) {
                    player->data.angle.z = cameraWorldYaw;
                    player->Update3DPosition(false);
                }

                g_savedFreeRotationValid = false;
                g_lastCameraRootWorldYawValid = false;
                RememberADSCameraRootWorldYaw();
            } else {
                const bool returningToHipFire =
                    player && WeaponDrawn(player) && camera && thirdPerson &&
                    CameraAccess::GetCurrentState(camera) == thirdPerson;
                float exitCameraYaw = 0.0F;
                const bool haveExitCameraYaw =
                    g_lastADSCameraRootWorldYawValid || ReadCameraRootWorldYaw(exitCameraYaw);
                if (g_lastADSCameraRootWorldYawValid) {
                    exitCameraYaw = g_lastADSCameraRootWorldYaw;
                }

                if (returningToHipFire && haveExitCameraYaw) {
                    const float finalADSYaw = player->data.angle.z;

                    const Point2 desiredFreeRotation{NormalizeRelativeAngle(exitCameraYaw), 0.0F};
                    StartTransitionReseed(thirdPerson, finalADSYaw, desiredFreeRotation);
                } else {
                    g_savedFreeRotationValid = false;
                    g_transitionReseedFrames = 0;
                }

                g_lastCameraRootWorldYawValid = false;
                g_lastADSCameraRootWorldYawValid = false;
            }

            g_wasAiming = aiming;
        }

        static void SyncAimReleaseBeforeCameraUpdate() {
            if (!g_wasAiming) {
                return;
            }

            void *camera = CameraAccess::GetPlayerCamera();
            void *thirdPerson = CameraAccess::GetThirdPersonState(camera);
            if (camera && thirdPerson && CameraAccess::GetCurrentState(camera) == thirdPerson &&
                !CameraAccess::IsIronSights(thirdPerson)) {
                SyncAimTransition();
            }
        }

        static void CameraStateBeginThunk(void *a_self) {
            // Capture the outgoing rendered view before native Begin resets TP.
            // Only use a prior first-person sample, never a loading/cinematic view.
            if (g_config.enabled && Gameplay() && g_lastFirstPersonCameraYawValid) {
                float yaw = 0;
                if (ReadCameraRootWorldYaw(yaw))
                    g_lastFirstPersonCameraYaw = yaw;
                if (auto *p = RE::PlayerCharacter::GetSingleton())
                    g_lastFirstPersonPitch = p->data.angle.x;
            }
            CameraStateBeginFunc(a_self);

            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!g_config.enabled || !Gameplay() || !player ||
                (!WeaponDrawn(player) && !(g_config.keepHolsterView &&
                                           player->weaponState == RE::WEAPON_STATE::kSheathed)) ||
                CameraAccess::IsIronSights(a_self) || !g_lastFirstPersonCameraYawValid) {
                g_seededOnThirdPersonBegin = false;
                return;
            }

            const float actorYaw = player->data.angle.z;
            const Point2 desiredFreeRotation{NormalizeRelativeAngle(g_lastFirstPersonCameraYaw),
                                             g_lastFirstPersonPitch - player->data.angle.x};
            if (player->weaponState == RE::WEAPON_STATE::kSheathed && g_config.keepHolsterView)
                g_holsteredOrbit = true;

            StartTransitionReseed(a_self, actorYaw, desiredFreeRotation);
            g_seededOnThirdPersonBegin = true;
        }

        static inline unsigned g_lockReleaseFrames = 0;
        static inline float g_releaseWorldPitch = 0;

        static void BeginLockRelease() {
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            if (!Gameplay() || !player || !camera ||
                !camera->IsStateActive(RE::CameraStates::kThirdPerson) || IronSightsActive() ||
                !MeleeLock::EquippedMelee(player))
                return;
            auto *state = static_cast<RE::ThirdPersonState *>(camera->GetState().get());
            if (!state)
                return;
            // Capture the last rendered direction before the next lock correction.
            // Use the same +Y forward and world-yaw convention as lock aiming.
            const auto forward = MeleeLock::Forward();
            const float worldPitch = -std::atan2(forward.z, std::hypot(forward.x, forward.y));
            g_releaseWorldPitch = worldPitch;
            const float worldYaw = std::atan2(forward.x, forward.y);
            if (!std::isfinite(worldYaw) || !std::isfinite(worldPitch))
                return;
            REX::LogInformation(
                "Lock release view: yaw={} pitch={} orbit=({}, {}) actor=({}, {}) anchors=({}, {})",
                worldYaw, worldPitch, state->freeRotation.x, state->freeRotation.y,
                player->data.angle.z, player->data.angle.x, state->targetYaw, state->currentYaw);
            g_savedFreeRotation = {NormalizeRelativeAngle(worldYaw),
                                   worldPitch - player->data.angle.x};
            state->freeRotation.x = g_savedFreeRotation.x;
            state->freeRotation.y = g_savedFreeRotation.y;
            g_savedFreeRotationValid = true;
            g_transitionReseedFrames = 0;
            g_lockReleaseFrames = 3;
            const auto weapon = player->weaponState;
            if (weapon == RE::WEAPON_STATE::kWantToSheathe ||
                weapon == RE::WEAPON_STATE::kSheathing || weapon == RE::WEAPON_STATE::kSheathed) {
                g_holsterCameraHeld = true;
                g_holsterReleaseFrames = 2;
                g_holsteredOrbit = g_config.keepHolsterView;
                state->targetYaw = state->currentYaw = player->data.angle.z;
            }
            TTPDiagnostic::Trace("Lock release: rebasing camera anchors into independent movement");
        }

        static void RebaseLockRelease(void *raw) {
            if (!g_lockReleaseFrames)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player || MeleeLock::lockRequested ||
                !(HolsterCameraActive(raw) || MeleeLock::ProtectCamera())) {
                g_lockReleaseFrames = 0;
                return;
            }
            auto *state = static_cast<RE::ThirdPersonState *>(raw);
            // Independent movement can immediately change actor yaw/pitch.
            // Rebase both interpolation anchors together, preserving orbit.
            state->targetYaw = player->data.angle.z;
            state->currentYaw = player->data.angle.z;
            g_savedFreeRotation.y = g_releaseWorldPitch - player->data.angle.x;
            state->freeRotation.x = g_savedFreeRotation.x;
            state->freeRotation.y = g_savedFreeRotation.y;
        }

        static bool ProtectedOrbit(void *state) {
            return ArmedCameraActive(state) || FavoriteCameraActive(state) ||
                   HolsterCameraActive(state) || HipFireCameraActive(state) ||
                   MeleeLock::ProtectCamera();
        }

        static void ProcessWeaponDrawnChangeThunk(void *state, bool drawn) {
            if (!drawn && !g_cameraWeaponSpoofActive && !g_locomotionSpoofActive &&
                MeleeLock::lockRequested && Gameplay()) {
                MeleeLock::Unlock();
                if (g_lockReleaseFrames && g_savedFreeRotationValid) {
                    g_holsterCameraHeld = true;
                    g_holsterReleaseFrames = 2;
                    g_holsteredOrbit = g_config.keepHolsterView;
                }
            }
            if (!ProtectedOrbit(state)) {
                ProcessWeaponDrawnChangeFunc(state, drawn);
                return;
            }
            auto *third = static_cast<RE::ThirdPersonState *>(state);
            const auto orbit = g_savedFreeRotationValid
                                   ? g_savedFreeRotation
                                   : Point2{third->freeRotation.x, third->freeRotation.y};
            const float anchor = third->targetYaw, current = third->currentYaw;
            // This callback is camera-only. Player weapon/animation processing
            // remains native; prevent the camera from treating our scoped state
            // changes as a request to align to the actor.
            ProcessWeaponDrawnChangeFunc(state, false);
            third->freeRotation.x = orbit.x;
            third->freeRotation.y = orbit.y;
            third->targetYaw = anchor;
            third->currentYaw = current;
            g_savedFreeRotation = orbit;
            g_savedFreeRotationValid = true;
        }

        static void SetFreeRotationModeThunk(void *a_self, bool a_cameraEnable,
                                             bool a_modifyRotation) {
            if (ProtectedOrbit(a_self))
                return;
            SetFreeRotationModeFunc(a_self, a_cameraEnable, a_modifyRotation);

            if (g_transitionReseedFrames > 0 && Active(a_self)) {
                ApplyTransitionReseed(a_self);
            }
        }

        static void CameraStateUpdateThunk(void *a_self,
                                           RE::BSTSmartPointer<RE::TESCameraState> &a_nextState) {
            ResumeAfterLoading();
            SyncAimReleaseBeforeCameraUpdate();
            if (!Active(a_self)) {
                g_savedFreeRotationValid = false;
                CameraStateUpdateFunc(a_self, a_nextState);
                WaterWeaponVisibility::Update(RE::PlayerCharacter::GetSingleton());
                return;
            }

            void *camera = CameraAccess::GetPlayerCamera();
            const auto stateBase = reinterpret_cast<std::uintptr_t>(a_self);
            const auto cameraBase = reinterpret_cast<std::uintptr_t>(camera);
            auto *freeRotation = reinterpret_cast<Point2 *>(stateBase + kTPFreeRotationOffset);
            auto *freeEnabled = reinterpret_cast<bool *>(stateBase + kTPFreeRotationEnabledOffset);
            auto *cameraReady =
                reinterpret_cast<bool *>(cameraBase + kPlayerCameraFreeRotationReadyOffset);

            if (g_transitionReseedFrames > 0) {
                ApplyTransitionReseed(a_self);
            } else if (g_savedFreeRotationValid) {
                *freeRotation = g_savedFreeRotation;
            }

            if (MeleeLock::lockRequested) {
                MeleeLock::AimCamera(static_cast<RE::ThirdPersonState *>(a_self));
                g_savedFreeRotation = *freeRotation;
                g_savedFreeRotationValid = true;
            }

            RebaseLockRelease(a_self);
            RememberCameraRootWorldYaw();
            const bool beforeEnabled = *freeEnabled;
            const bool beforeReady = *cameraReady;

            *freeEnabled = true;
            *cameraReady = true;
            {
                ScopedFavoriteCameraWeapon cameraWeapon(a_self);
                CameraStateUpdateFunc(a_self, a_nextState);
            }

            if (g_transitionReseedFrames > 0) {
                ApplyTransitionReseed(a_self);
                --g_transitionReseedFrames;
            } else {
                g_savedFreeRotation = *freeRotation;
                g_savedFreeRotationValid = true;
            }

            if (Active(a_self)) {
                RememberCameraRootWorldYaw();
            }

            *freeEnabled = beforeEnabled;
            *cameraReady = beforeReady;
            if (g_lockReleaseFrames)
                --g_lockReleaseFrames;
            FinishHolsterCameraUpdate();
            WaterWeaponVisibility::Update(RE::PlayerCharacter::GetSingleton());
            if (g_hipFireReleaseFrames > 0)
                --g_hipFireReleaseFrames;
        }

        static void HandleLookInputThunk(void *a_self, const Point2 &a_input) {
            Point2 input = a_input;
            if (g_config.enabled && Gameplay()) {
                const auto* player = RE::PlayerCharacter::GetSingleton();
                const bool aiming = CameraAccess::IsIronSights(a_self) ||
                    (player && (player->gunState == RE::GUN_STATE::kSighted ||
                                player->gunState == RE::GUN_STATE::kFireSighted));
                const auto& profile = LookDevice::controller ? g_config.controllerSensitivity
                                                            : g_config.mouseSensitivity;
                const auto& axes = profile.Select(aiming, LookDevice::armed);
                input.x *= axes.x;
                input.y *= axes.y;
            }
            if (g_loading || g_resumeAfterLoading || !GameWindowFocused() || ConsoleOpen() ||
                SettingsOpen())
                return;
            if (MeleeLock::Active())
                return;
            ResumeAfterLoading();
            SyncAimReleaseBeforeCameraUpdate();
            if (!Active(a_self)) {
                g_savedFreeRotationValid = false;
                HandleLookInputFunc(a_self, input);
                return;
            }

            void *camera = CameraAccess::GetPlayerCamera();
            const auto stateBase = reinterpret_cast<std::uintptr_t>(a_self);
            const auto cameraBase = reinterpret_cast<std::uintptr_t>(camera);
            auto *freeRotation = reinterpret_cast<Point2 *>(stateBase + kTPFreeRotationOffset);
            auto *freeEnabled = reinterpret_cast<bool *>(stateBase + kTPFreeRotationEnabledOffset);
            auto *cameraReady =
                reinterpret_cast<bool *>(cameraBase + kPlayerCameraFreeRotationReadyOffset);

            if (g_transitionReseedFrames > 0) {
                ApplyTransitionReseed(a_self);
            } else if (g_savedFreeRotationValid) {
                *freeRotation = g_savedFreeRotation;
            }

            RebaseLockRelease(a_self);
            RememberCameraRootWorldYaw();
            const bool beforeEnabled = *freeEnabled;
            const bool beforeReady = *cameraReady;

            *freeEnabled = true;
            *cameraReady = true;
            {
                ScopedFavoriteCameraWeapon cameraWeapon(a_self);
                HandleLookInputFunc(a_self, input);
            }

            if (g_transitionReseedFrames > 0) {
                ApplyTransitionReseed(a_self);
            } else {
                g_savedFreeRotation = *freeRotation;
                g_savedFreeRotationValid = true;
                if (g_lockReleaseFrames) {
                    auto *player = RE::PlayerCharacter::GetSingleton();
                    if (player)
                        g_releaseWorldPitch = player->data.angle.x + freeRotation->y;
                }
            }
            if (Active(a_self)) {
                RememberCameraRootWorldYaw();
            }

            *freeEnabled = beforeEnabled;
            *cameraReady = beforeReady;
        }

        static void UpdateRotationThunk(void *a_self) {
            ResumeAfterLoading();
            SyncAimReleaseBeforeCameraUpdate();
            if (!Active(a_self)) {
                g_savedFreeRotationValid = false;
                UpdateRotationFunc(a_self);
                return;
            }

            void *camera = CameraAccess::GetPlayerCamera();
            const auto stateBase = reinterpret_cast<std::uintptr_t>(a_self);
            const auto cameraBase = reinterpret_cast<std::uintptr_t>(camera);
            auto *freeRotation = reinterpret_cast<Point2 *>(stateBase + kTPFreeRotationOffset);
            auto *freeEnabled = reinterpret_cast<bool *>(stateBase + kTPFreeRotationEnabledOffset);
            auto *cameraReady =
                reinterpret_cast<bool *>(cameraBase + kPlayerCameraFreeRotationReadyOffset);

            if (g_transitionReseedFrames > 0) {
                ApplyTransitionReseed(a_self);
            } else if (g_savedFreeRotationValid) {
                *freeRotation = g_savedFreeRotation;
            }

            RebaseLockRelease(a_self);
            RememberCameraRootWorldYaw();
            const bool beforeEnabled = *freeEnabled;
            const bool beforeReady = *cameraReady;

            *freeEnabled = true;
            *cameraReady = true;
            {
                ScopedFavoriteCameraWeapon cameraWeapon(a_self);
                UpdateRotationFunc(a_self);
            }

            if (g_transitionReseedFrames > 0) {
                ApplyTransitionReseed(a_self);
            } else {
                g_savedFreeRotation = *freeRotation;
                g_savedFreeRotationValid = true;
            }
            if (Active(a_self)) {
                RememberCameraRootWorldYaw();
            }

            *freeEnabled = beforeEnabled;
            *cameraReady = beforeReady;
        }

        static bool Install() {
            if (g_cameraHooksInstalled.load(std::memory_order_acquire)) {
                return true;
            }

            void *camera = CameraAccess::GetPlayerCamera();
            void *thirdPerson = CameraAccess::GetThirdPersonState(camera);
            if (!thirdPerson) {
                return false;
            }

            auto **vtable = *reinterpret_cast<void ***>(thirdPerson);
            if (!vtable || !vtable[kCameraStateBeginIndex] || !vtable[kCameraStateUpdateIndex] ||
                !vtable[kProcessWeaponDrawnChangeIndex] || !vtable[kSetFreeRotationModeIndex] ||
                !vtable[kUpdateRotationIndex] || !vtable[kHandleLookInputIndex]) {
                REX::LogError("ThirdPersonState camera vtable entries were null");
                return false;
            }

            HookBatch hooks(reinterpret_cast<std::uintptr_t *>(vtable));
            hooks.Add(kCameraStateBeginIndex, CameraStateBeginThunk, CameraStateBeginFunc);
            hooks.Add(kCameraStateUpdateIndex, CameraStateUpdateThunk, CameraStateUpdateFunc);
            hooks.Add(kProcessWeaponDrawnChangeIndex, ProcessWeaponDrawnChangeThunk,
                      ProcessWeaponDrawnChangeFunc);
            hooks.Add(kSetFreeRotationModeIndex, SetFreeRotationModeThunk, SetFreeRotationModeFunc);
            hooks.Add(kUpdateRotationIndex, UpdateRotationThunk, UpdateRotationFunc);
            // Chain the current HandleLookInput slot instead of disabling it. Commonwealth Camera
            // loads before TTP in the tested setup, so HookBatch captures its current vtable target
            // as HandleLookInputFunc. TTP then processes the same mouse input and forwards to the
            // captured Commonwealth Camera/native implementation.
            hooks.Add(kHandleLookInputIndex, HandleLookInputThunk, HandleLookInputFunc);
            if (!hooks.Commit())
                return false;
            g_cameraHooksInstalled.store(true, std::memory_order_release);
            return true;
        }

        static inline bool g_loading = false;
        static inline bool g_resumeAfterLoading = false;
        static inline bool g_loadOrbitValid = false;
        static inline Point2 g_loadRelativeOrbit{};

        static void LoadingChanged(bool opening) {
            if (!opening) {
                g_loading = false;
                g_resumeAfterLoading = g_loadOrbitValid;
                return;
            }
            if (g_loading)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            auto *state = CameraAccess::GetThirdPersonState(camera);
            const bool preserve = g_gameReady && g_config.enabled && player && camera && state &&
                                  camera->IsStateActive(RE::CameraStates::kThirdPerson) &&
                                  !CameraAccess::IsIronSights(state) && g_savedFreeRotationValid;
            Point2 relative{};
            if (preserve) {
                relative = {NormalizeRelativeAngle(g_savedFreeRotation.x - player->data.angle.z),
                            g_savedFreeRotation.y};
            }
            ADSTurn::Cancel();
            Reset();
            g_loading = true;
            g_loadOrbitValid = preserve;
            g_loadRelativeOrbit = relative;
            TTPDiagnostic::Trace("Loading: retired camera samples from departing location");
        }

        static void ResumeAfterLoading() {
            if (g_loading || !g_resumeAfterLoading || !Gameplay())
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            if (!player || !camera)
                return;
            if (!camera->IsStateActive(RE::CameraStates::kThirdPerson)) {
                g_resumeAfterLoading = false;
                g_loadOrbitValid = false;
                return;
            }
            auto *state = CameraAccess::GetThirdPersonState(camera);
            if (!state)
                return;
            g_resumeAfterLoading = false;
            g_loadOrbitValid = false;
            if (CameraAccess::IsIronSights(state))
                return;
            const Point2 orbit{NormalizeRelativeAngle(player->data.angle.z + g_loadRelativeOrbit.x),
                               g_loadRelativeOrbit.y};
            g_holsteredOrbit = g_config.keepHolsterView && !WeaponDrawn(player);
            g_drawOrbit = WeaponDrawn(player);
            g_lastRealDrawn = WeaponDrawn(player);
            g_wasThirdPerson = true;
            StartTransitionReseed(state, player->data.angle.z, orbit);
            TTPDiagnostic::Trace("Loading: rebased orbit to arrival facing");
        }

        static void Reset() {
            g_loading = false;
            g_resumeAfterLoading = false;
            g_loadOrbitValid = false;
            MeleeLock::Reset();
            g_lockReleaseFrames = 0;
            g_holsteredOrbit = false;
            g_drawOrbit = false;
            ResetHipFire();
            g_holsterCameraHeld = false;
            g_lastRealDrawn = false;
            g_holsterReleaseFrames = 0;
            g_favoriteCameraHeld = false;
            g_savedFreeRotationValid = false;
            g_lastCameraRootWorldYawValid = false;
            g_lastADSCameraRootWorldYawValid = false;
            g_lastFirstPersonCameraYawValid = false;
            g_transitionReseedFrames = 0;
            g_seededOnThirdPersonBegin = false;
            g_wasThirdPerson = false;
            g_wasAiming = false;
        }

        static inline Point2 g_savedFreeRotation{};
        static inline bool g_savedFreeRotationValid{false};
        static inline float g_lastCameraRootWorldYaw{0.0F};
        static inline bool g_lastCameraRootWorldYawValid{false};
        static inline float g_lastADSCameraRootWorldYaw{0.0F};
        static inline bool g_lastADSCameraRootWorldYawValid{false};
        static inline Point2 g_transitionFreeRotation{};
        static inline float g_transitionAnchorYaw{0.0F};
        static inline std::uint32_t g_transitionReseedFrames{0};
        static inline bool g_seededOnThirdPersonBegin{false};
        static inline bool g_wasThirdPerson{false};
        static inline float g_lastFirstPersonCameraYaw{0.0F};
        static inline float g_lastFirstPersonPitch{0.0F};
        static inline bool g_lastFirstPersonCameraYawValid{false};
        static inline bool g_wasAiming{false};
        static inline decltype(&CameraStateBeginThunk) CameraStateBeginFunc = nullptr;
        static inline decltype(&CameraStateUpdateThunk) CameraStateUpdateFunc = nullptr;
        static inline decltype(&ProcessWeaponDrawnChangeThunk) ProcessWeaponDrawnChangeFunc =
            nullptr;
        static inline decltype(&SetFreeRotationModeThunk) SetFreeRotationModeFunc = nullptr;
        static inline decltype(&UpdateRotationThunk) UpdateRotationFunc = nullptr;
        static inline decltype(&HandleLookInputThunk) HandleLookInputFunc = nullptr;
    };

    // Log state changes at most four times per second.
    // Does not change game state or call target selection/facing functions.
    void TraceMovementState(RE::PlayerCharacter *p) {
        if (!p || !Gameplay() || g_locomotionSpoofActive || g_cameraWeaponSpoofActive)
            return;
        static double next = 0;
        static unsigned previous = ~0U;
        const double now = EquipGuard::Now();
        if (now < next)
            return;
        next = now + 0.25;
        const auto *camera = RE::PlayerCamera::GetSingleton();
        const bool third = camera && camera->IsStateActive(RE::CameraStates::kThirdPerson);
        const bool filter =
            static_cast<const RE::IMovementPlayerControlsFilter *>(p)->IsInThirdPerson();
        const unsigned weapon = static_cast<unsigned>(p->weaponState);
        const bool guard = EquipGuard::Active();
        const bool hip = HipFireMovementActive(p);
        const bool lock = MeleeLock::lockRequested;
        const bool ads = IronSightsActive();
        const unsigned bits = weapon | (unsigned(third) << 4) | (unsigned(filter) << 5) |
                              (unsigned(guard) << 6) | (unsigned(hip) << 7) |
                              (unsigned(lock) << 8) | (unsigned(ads) << 9) |
                              (unsigned(p->sprinting) << 10);
        if (bits == previous)
            return;
        previous = bits;
        char line[256]{};
        std::snprintf(
            line, sizeof(line),
            "Movement diagnostic t=%.3f weapon=%u cameraThird=%d filterThird=%d favorite=%d hip=%d lock=%d ads=%d sprint=%d",
            now, weapon, int(third), int(filter), int(guard), int(hip), int(lock), int(ads),
            int(p->sprinting));
        TTPDiagnostic::Trace(line);
    }

    // Cache the physical input until Actor::Update has consumed the remapped
    // forward magnitude. Never use that remapped vector to choose a heading.
    struct DirectionalInput {
        static inline RE::NiPoint2 raw{0.0F, 0.0F};
        static inline float magnitude = 0.0F;
        static inline bool valid = false;
        static void Restore() {
            auto *controls = RE::PlayerControls::GetSingleton();
            if (valid && controls && controls->data.moveInputVec.x == 0.0F &&
                controls->data.moveInputVec.y == magnitude)
                controls->data.moveInputVec = raw;
            valid = false;
        }
        static void Prepare(RE::PlayerCharacter *player) {
            Restore();
            auto *controls = RE::PlayerControls::GetSingleton();
            if (!controls || !IndependentLocomotionActive(player))
                return;
            raw = controls->data.moveInputVec;
            magnitude = std::hypot(raw.x, raw.y);
            if (!std::isfinite(magnitude))
                return;
            valid = true;
            controls->data.moveInputVec = RE::NiPoint2{0.0F, magnitude};
        }
    };

    struct DirectionalHooks {
        static void Movement(RE::PlayerCharacter *self, float dt, RE::NiPoint3 &move,
                             RE::NiPoint3 &angle) {
            movementOriginal(self, dt, move, angle);
            if (self != RE::PlayerCharacter::GetSingleton())
                return;
            if (g_directionalHooksInstalled.load() && InPowerArmor(self) &&
                HipFireMovementActive(self)) {
                auto *camera = RE::PlayerCamera::GetSingleton();
                auto *third = camera ? static_cast<RE::ThirdPersonState *>(
                                          CameraAccess::GetThirdPersonState(camera)) : nullptr;
                if (third && CameraAccess::GetCurrentState(camera) == third) {
                    const float target = ThirdPersonCameraOrbitHook::g_savedFreeRotationValid
                                             ? ThirdPersonCameraOrbitHook::g_savedFreeRotation.x
                                             : third->freeRotation.x;
                    if (std::isfinite(target) && std::isfinite(self->data.angle.z) &&
                        std::isfinite(dt) && dt > 0) {
                        const float next = g_config.smoothHipFire
                                               ? FacingBlend::Step(self->data.angle.z, target, dt)
                                               : Logic::Normalize(target);
                        angle.z = Logic::SignedYaw(next - self->data.angle.z);
                    }
                }
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
            if (!ThirdPersonCameraOrbitHook::ReadCameraRootWorldYaw(yaw))
                return;
            // Only engine yaw delta is changed. Translation, jump physics and
            // all camera state remain owned by their existing paths.
            if (ADSTurn::pending) {
                angle.z = 0;
                return;
            }
            angle.z = DirectionalMovement::TurnDelta(input.x, input.y, yaw, self->data.angle.z, dt);
        }
        static bool Stance(const RE::IMovementPlayerControlsFilter *self) {
            const bool native = stanceOriginal(self);
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (player && self == static_cast<const RE::IMovementPlayerControlsFilter *>(player) &&
                IndependentLocomotionActive(player))
                return false;
            return native;
        }
        static bool Install() {
            if (g_directionalHooksInstalled.load())
                return true;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player)
                return false;
            // Keep successful partial installation inert until both are ready.
            static bool movementInstalled = false;
            static bool stanceInstalled = false;
            if (!movementInstalled) {
                HookBatch hooks(*reinterpret_cast<std::uintptr_t **>(player));
                hooks.Add(0x125, Movement, movementOriginal);
                movementInstalled = hooks.Commit();
            }
            if (!stanceInstalled) {
                auto *filter = static_cast<RE::IMovementPlayerControlsFilter *>(player);
                HookBatch hooks(*reinterpret_cast<std::uintptr_t **>(filter));
                hooks.Add(0x03, Stance, stanceOriginal);
                stanceInstalled = hooks.Commit();
            }
            g_directionalHooksInstalled.store(movementInstalled && stanceInstalled);
            return g_directionalHooksInstalled.load();
        }
        static inline decltype(&Movement) movementOriginal = nullptr;
        static inline decltype(&Stance) stanceOriginal = nullptr;
    };

    // Native heading = orbit yaw + actor yaw unless the holstered/free-movement
    // predicate succeeds. Our independent orbit already contains world yaw.
    // Override only the two heading call sites, never the predicate globally.
    struct CompassHeadingHook {
        static inline bool installed = false;
        static inline bool attempted = false;
        static inline bool (*original)(RE::Actor *) = nullptr;
        static inline REL::Trampoline trampoline{"TrueThirdPerson compass"};
        static bool UseCameraHeading(RE::Actor *actor) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            // Heading selection must not depend on transient movement/menu eligibility.
            return g_config.enabled && g_config.cameraCompass && g_cameraHooksInstalled.load() &&
                   actor == player && player && camera &&
                   camera->IsStateActive(RE::CameraStates::kThirdPerson) &&
                   !CameraAccess::IsIronSights(CameraAccess::GetThirdPersonState(camera));
        }
        static bool Thunk(RE::Actor *actor) {
            return UseCameraHeading(actor) ? true : original(actor);
        }
        static bool CompassPivotThunk(RE::Actor *actor) {
            // The heading producers otherwise write zero during a pivot and
            // skip the orbit calculation. Do not change the actor's pivot flag.
            return UseCameraHeading(actor) ? false : actor->ShouldPivotToFaceCamera();
        }
        static bool Install() {
            if (attempted)
                return installed;
            attempted = true;
            if (!g_config.cameraCompass)
                return false;
            // Call structure verified on 1.11.240. Byte guards also apply on 1.11.221.
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            // Resolve containing functions for this runtime; validate each interior
            // call before patching. No executable-version RVAs are reused.
            const auto &database = REL::Iddb::GetSingleton();
            const auto predicate = base + database->GetOffset(2230295);
            const bool og = F4SE::IsRuntimeOnlyOG();
            // OG shares its heading producer; AE also inlines it in Update.
            const auto layout = NativeHookLayout::Compass(og);
            std::vector<std::uintptr_t> sites;
            std::vector<std::uintptr_t> pivotSites;
            for (const auto& site : layout) {
                const auto function = database->GetOffset(site.function);
                sites.push_back(function + site.callOffset);
                pivotSites.push_back(function + site.pivotOffset);
            }
            constexpr std::array<std::uint8_t, 12> tail{0x84, 0xC0, 0x74, 0x05, 0x0F, 0x57,
                                                        0xC0, 0xEB, 0x0C, 0x48, 0x8B, 0x03};
            for (auto rva : sites) {
                const auto *code = reinterpret_cast<const std::uint8_t *>(base + rva);
                std::int32_t relative = 0;
                std::memcpy(&relative, code + 1, sizeof(relative));
                if (code[0] != 0xE8 || base + rva + 5 + relative != predicate ||
                    !std::equal(tail.begin(), tail.end(), code + 5)) {
                    TTPDiagnostic::Trace(
                        "Compass heading call-site validation failed; no patches applied");
                    return false;
                }
            }
            constexpr std::array<std::uint8_t, 9> pivotPrefix{
                0xFF, 0x90, 0xA8, 0x08, 0x00, 0x00, 0x84, 0xC0, 0x74};
            for (std::size_t i = 0; i < pivotSites.size(); ++i) {
                const auto* code = reinterpret_cast<const std::uint8_t*>(base + pivotSites[i]);
                if (std::memcmp(code, pivotPrefix.data(), pivotPrefix.size()) != 0 ||
                    code[pivotPrefix.size()] != layout[i].pivotBranch) {
                    TTPDiagnostic::Trace(
                        "Compass pivot call-site validation failed; no patches applied");
                    return false;
                }
            }
            original = reinterpret_cast<decltype(original)>(predicate);
            trampoline.Create(64);
            for (auto rva : sites)
                trampoline.WriteCall<5>(base + rva, Thunk);
            // Original calls are vtable-indirect, not RIP-relative. Construct
            // Call6 directly instead of asking WriteCall6 to decode an old target.
            const auto pivotTarget =
                trampoline.AllocateBranch6(reinterpret_cast<std::uintptr_t>(&CompassPivotThunk));
            for (auto rva : pivotSites) {
                const REL::Asm::Call6 call(base + rva, pivotTarget);
                if (REL::WriteSafeData(base + rva, call).value() != REX::ERROR_NUMBER_SUCCESS)
                    REX::Fail("Failed to install compass pivot call");
            }
            installed = true;
            TTPDiagnostic::Trace(
                "Compass: holstered heading and pivot bypass installed at all runtime producers");
            return true;
        }
    };

    struct ADSStateHook {
        static inline bool (*original)(RE::ActorState *, bool) = nullptr;
        static bool Eligible(RE::PlayerCharacter *player) {
            if (!player || !g_config.adsTurnMs ||
                !IndependentFacingActive(player) ||
                player->lifeState != RE::ACTOR_LIFE_STATE::kAlive || player->swimming ||
                player->weaponState != RE::WEAPON_STATE::kDrawn || !player->biped ||
                EquipGuard::Now() < EquipGuard::until || MeleeLock::EquippedMelee(player))
                return false;
            auto *camera = RE::PlayerCamera::GetSingleton();
            const auto *gun = player->biped->GetBipObject(RE::BIPED_OBJECT::kWeaponGun);
            return camera && camera->IsStateActive(RE::CameraStates::kThirdPerson) && gun &&
                   gun->parent.object && gun->partClone;
        }
        static bool Thunk(RE::ActorState *self, bool sighted) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player || self != static_cast<RE::ActorState *>(player))
                return original(self, sighted);
            if (!sighted) {
                if (ADSTurn::pending)
                    TTPDiagnostic::Trace("ADS engine request cancelled");
                ADSTurn::Cancel();
                return original(self, false);
            }
            if (!Eligible(player)) {
                ADSTurn::Cancel();
                return original(self, true);
            }
            if (!ADSTurn::pending) {
                if (player->gunState == RE::GUN_STATE::kSighted ||
                    player->gunState == RE::GUN_STATE::kFireSighted)
                    return original(self, true);
                ThirdPersonCameraOrbitHook::ResetHipFire();
                ADSTurn::Begin(player->data.angle.z, g_config.adsTurnMs / 1000.0F);
                TTPDiagnostic::Trace("ADS engine request: pre-turn started");
            }
            // The engine has not entered sights yet. Complete through the
            // original state setter once the body has reached the camera heading.
            return false;
        }
        static void Tick(RE::PlayerCharacter *player, float dt) {
            if (!ADSTurn::pending)
                return;
            if (!Eligible(player)) {
                ADSTurn::Cancel();
                return;
            }
            float target = 0;
            if (!ThirdPersonCameraOrbitHook::ReadCameraRootWorldYaw(target)) {
                ADSTurn::Cancel();
                return;
            }
            const bool done = ADSTurn::Step(target, dt);
            player->data.angle.z = ADSTurn::yaw;
            if (done) {
                ADSTurn::Cancel();
                const bool entered = original(static_cast<RE::ActorState *>(player), true);
                TTPDiagnostic::Trace(entered ? "ADS engine handoff accepted"
                                             : "ADS engine handoff rejected");
            }
        }
        static bool Install() {
            if (original)
                return true;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player)
                return false;
            auto *state = static_cast<RE::ActorState *>(player);
            HookBatch hooks(*reinterpret_cast<std::uintptr_t **>(state));
            hooks.Add(0x25, Thunk, original);
            if (hooks.Commit())
                return true;
            original = nullptr;
            return false;
        }
    };

    struct PlayerUpdateHook {
        static void Thunk(RE::PlayerCharacter *a_self, float a_delta) {
            ThirdPersonCameraOrbitHook::Install();
            // Retire a completed favorite even if armed camera protection
            // short-circuited FavoriteCameraActive on the previous frame.
            if (!g_cameraWeaponSpoofActive && !FavoriteSwitchInProgress(a_self))
                g_favoriteCameraHeld = false;
            PowerArmorCamera::Install();
            CompassHeadingHook::Install();
            LockMarker::Ensure();
            ADSStateHook::Tick(a_self, a_delta);
            TraceMovementState(a_self);
            MeleeLock::Tick(a_self, a_delta);
            ThirdPersonCameraOrbitHook::ObserveHolster();
            ThirdPersonCameraOrbitHook::RefreshHipFire(a_self);
            ThirdPersonCameraOrbitHook::FaceHipFire(a_self, a_delta);
            if (Gameplay())
                ThirdPersonCameraOrbitHook::SyncAimTransition();

            DirectionalInput::Prepare(a_self);
            // Real weapon state reaches native equip/animation processing.
            Func(a_self, a_delta);
            DirectionalInput::Restore();
            if (ADSTurn::pending)
                a_self->data.angle.z = ADSTurn::yaw;
            WaterWeaponVisibility::Update(a_self);
            MeleeLock::RestoreFacing(a_self);
            ThirdPersonCameraOrbitHook::FaceHipFire(a_self);
            ThirdPersonCameraOrbitHook::BlendMovementReturn(a_self, a_delta);
        }

        static bool Install() {
            static bool installed = false;
            if (installed)
                return true;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player)
                return false;
            HookBatch hooks(*reinterpret_cast<std::uintptr_t **>(player));
            hooks.Add(kPlayerUpdateIndex, Thunk, Func);
            installed = hooks.Commit();
            return installed;
        }

        static inline decltype(&Thunk) Func = nullptr;
    };

    struct PlayerControlsOutputHook {
        static void Thunk(RE::IMovementPlayerControls *a_self, std::uint32_t a_numericID,
                          RE::PlayerControlsMovementData &a_output) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (Gameplay())
                ThirdPersonCameraOrbitHook::SyncAimTransition();

            DirectionalInput::Prepare(player);
            Func(a_self, a_numericID, a_output);
            auto *controls = RE::PlayerControls::GetSingleton();
            if (!player || !controls || !IndependentLocomotionActive(player) ||
                player->weaponState != RE::WEAPON_STATE::kDrawn || EquipGuard::Active() ||
                player->DoGetCharacterState() != RE::IMovementState::CHARACTER_STATE::kOnGround)
                return;
            const auto input =
                DirectionalInput::valid ? DirectionalInput::raw : controls->data.moveInputVec;
            if (!IdleRotation::Stationary(input.x, input.y, a_output.movementSpeed))
                return;
            const float oldSpeed = a_output.rotationSpeed.z;
            const float oldTarget = a_output.targetAngle.z;
            if (!IdleRotation::ClearCameraTurn(a_output, player->data.angle.z))
                return;
            // Log suppressed rotation requests at most once per second.
            static double nextTrace = 0;
            const double now = EquipGuard::Now();
            if (now >= nextTrace &&
                (std::abs(oldSpeed) > 0.001F ||
                 std::abs(Logic::SignedYaw(oldTarget - player->data.angle.z)) > 0.001F)) {
                nextTrace = now + 1.0;
                char line[192]{};
                std::snprintf(line, sizeof(line),
                              "Idle rotation output: speedZ=%.4f targetZ=%.4f actorZ=%.4f",
                              oldSpeed, oldTarget, player->data.angle.z);
                TTPDiagnostic::Trace(line);
            }
        }

        static bool Install() {
            if (g_controlsHookInstalled.load(std::memory_order_acquire)) {
                return true;
            }

            auto *controls = RE::PlayerControls::GetSingleton();
            if (!controls) {
                return false;
            }

            auto *movementControls = static_cast<RE::IMovementPlayerControls *>(controls);
            HookBatch hooks(*reinterpret_cast<std::uintptr_t **>(movementControls));
            hooks.Add(kGetControllerOutputIndex, Thunk, Func);
            if (!hooks.Commit())
                return false;

            g_controlsHookInstalled.store(true, std::memory_order_release);
            return true;
        }

        static inline decltype(&Thunk) Func = nullptr;
    };

    struct LoadingObserver final : RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        RE::BSEventNotifyControl
        ProcessEvent(const RE::MenuOpenCloseEvent &event,
                     RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override {
            if (event.menuName == "LoadingMenu")
                ThirdPersonCameraOrbitHook::LoadingChanged(event.opening);
            return RE::BSEventNotifyControl::kContinue;
        }
        static void Install() {
            static LoadingObserver observer;
            static bool installed = false;
            auto *ui = RE::UI::GetSingleton();
            if (!installed && ui)
                installed =
                    static_cast<RE::BSTEventSource<RE::MenuOpenCloseEvent> *>(ui)->RegisterSink(
                        &observer);
        }
    };

    void MessageHandler(F4SE::MessagingInterface::Message *message) {
        if (!message)
            return;
        using Type = F4SE::MessagingInterface::MessageType;
        switch (message->GetType()) {
        case Type::kPreLoadGame:
            TTPDiagnostic::Trace("PreLoadGame: resetting gameplay state");
            ADSTurn::Cancel();
            g_gameReady = false;
            DirectionalInput::Restore();
            ThirdPersonCameraOrbitHook::Reset();
            WaterWeaponVisibility::Reset();
            return;
        case Type::kPostLoadGame:
        case Type::kNewGame:
            TTPDiagnostic::Trace("PostLoadGame/NewGame: enabling gameplay state");
            ADSTurn::Cancel();
            DirectionalInput::Restore();
            ThirdPersonCameraOrbitHook::Reset();
            WaterWeaponVisibility::Reset();
            g_gameReady = true;
            break;
        case Type::kGameDataReady:
            TTPDiagnostic::Trace("GameDataReady received");
            break;
        default:
            return;
        }
        if (!g_config.enabled) {
            TTPDiagnostic::Trace("Gameplay disabled by INI Enabled=0");
            return;
        }
        LoadingObserver::Install();
        TTPDiagnostic::Trace("Camera hook installation begin");
        const bool camera = ThirdPersonCameraOrbitHook::Install();
        TTPDiagnostic::Trace(camera ? "Camera hooks installed"
                                    : "Camera hooks unavailable or installation failed");
        TTPDiagnostic::Trace("Movement output hook installation begin");
        const bool movement = PlayerControlsOutputHook::Install();
        TTPDiagnostic::Trace(movement ? "Movement output hook installed"
                                      : "Movement output hook unavailable or installation failed");
        TTPDiagnostic::Trace(DirectionalHooks::Install()
                                 ? "Directional movement and stance hooks installed"
                                 : "Directional movement disabled: hook installation incomplete");
        TTPDiagnostic::Trace("Player update hook installation begin");
        const bool update = PlayerUpdateHook::Install();
        TTPDiagnostic::Trace(update ? "Player update hook installed"
                                    : "Player update hook unavailable or installation failed");
        TTPDiagnostic::Trace(ADSStateHook::Install() ? "ADS state hook installed"
                                                     : "ADS state hook unavailable");
        TTPDiagnostic::Trace(PowerArmorCamera::Install() ? "Power armor camera hook installed"
                                                        : "Power armor camera hook unavailable");
        TTPDiagnostic::Trace(CompassHeadingHook::Install() ? "Compass heading hook installed"
                                                           : "Compass heading hook unavailable");
        LockMarker::Ensure();
        EquipGuard::onButton = ThirdPersonCameraOrbitHook::HipFireButton;
        EquipGuard::onFavorite = ThirdPersonCameraOrbitHook::BeginFavoriteCamera;
        TTPDiagnostic::Trace(EquipGuard::InputObserver::Install()
                                 ? "Favorites input observer installed"
                                 : "Favorites input observer unavailable");
        MeleeLock::onRelease = ThirdPersonCameraOrbitHook::BeginLockRelease;
        EquipGuard::filterInput = MeleeLock::Input;
        MeleeLock::ready =
            EquipGuard::InputObserver::original && MeleeLock::CameraInputHook::Install();
        TTPDiagnostic::Trace(MeleeLock::ready ? "Melee lock: both input receivers ready"
                                              : "Melee lock disabled: input hook unavailable");
        TTPDiagnostic::Trace("Gameplay setup callback complete");
    }
} // namespace TrueThirdPerson

F4SE_PLUGIN_VERSION = []() consteval noexcept {
    F4SE::PluginVersionData info;
    info.SetPluginName("TrueThirdPerson");
    info.SetPluginAuthor("Ai Gen; based on supplied Independent Weapon Facing v4.7 and v7.2");
    info.SetPluginVersion(REX::Version(0, 2, 4, 46));
    info.SetUseSignatureScanning(true); // Runtime Database, not Address Library
    // Explicit runtime allowlist; CommonLib selects the matching OG/AE APIs.
    // Leave the AE-only structure flag clear for the legacy F4SE loader.
    info.SetCompatibleVersions(TrueThirdPerson::kTargetRuntimes);
    return info;
}();
// F4SE 0.6.x uses Query rather than the newer version-data export.
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* f4se, F4SE::PluginInfo* info) {
    if (!f4se || !info)
        return false;
    info->SetDataVersion(F4SE::PluginInfo::DATA_VERSION);
    info->SetPluginName("TrueThirdPerson");
    info->SetPluginVersion(REX::Version(0, 2, 4, 46));
    return !f4se->IsEditor() &&
           (f4se->GetRuntimeVersion() == REX::Version(1, 10, 163, 0) ||
            f4se->GetRuntimeVersion() == REX::Version(1, 11, 221, 0) ||
            f4se->GetRuntimeVersion() == REX::Version(1, 11, 240, 0));
}
F4SE_PLUGIN_LOAD(const F4SE::LoadInterface *f4se) {
    TTPDiagnostic::Trace("=== TrueThirdPerson 0.2.4.46 optional trampoline and look sensitivity test ===");
    if (!f4se || f4se->IsEditor() ||
        (f4se->GetRuntimeVersion() != REX::Version(1, 10, 163, 0) &&
         f4se->GetRuntimeVersion() != REX::Version(1, 11, 221, 0) &&
         f4se->GetRuntimeVersion() != REX::Version(1, 11, 240, 0))) {
        TTPDiagnostic::Trace("Rejected: missing interface, editor, or runtime mismatch");
        return false;
    }
    try {
        TTPDiagnostic::Trace("Runtime accepted; F4SE::Init begin");
        F4SE::Init(f4se, {.logName = "TrueThirdPerson", .logLevel = REX::LogLevel::kInformation});
        TTPDiagnostic::Trace("Runtime Database initialized; Address Library is not used");
        TTPDiagnostic::Trace("F4SE::Init returned; LoadConfig begin");
        TrueThirdPerson::LoadConfig();
        TTPDiagnostic::Trace("LoadConfig OK; RegisterListener begin");
        const bool registered = F4SE::GetMessagingInterface()->RegisterListener(
            TrueThirdPerson::MessageHandler, "F4SE");
        TTPDiagnostic::Trace(registered ? "RegisterListener OK; returning true"
                                        : "RegisterListener FAILED; returning false");
        return registered;
    } catch (const std::exception &error) {
        TTPDiagnostic::Trace("C++ exception escaped initialization; returning false");
        TTPDiagnostic::Trace(error.what());
        return false;
    } catch (...) {
        TTPDiagnostic::Trace("Unknown C++ exception escaped initialization; returning false");
        return false;
    }
}
