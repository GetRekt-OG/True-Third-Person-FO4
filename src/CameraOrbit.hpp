#pragma once

// Owns the third-person orbit: keeps the camera's free rotation independent of
// actor facing across draw/holster, ADS, favorites, hip fire, loading screens,
// VATS, workbenches and target lock.
namespace TrueThirdPerson {
    struct CameraOrbit {
        using State = RE::ThirdPersonState;

        static constexpr std::size_t kButtonInputIndex = 0x08;
        static constexpr std::size_t kBeginIndex = 0x09;
        static constexpr std::size_t kUpdateIndex = 0x0B;
        static constexpr std::size_t kWeaponDrawnChangeIndex = 0x11;
        static constexpr std::size_t kSetFreeRotationModeIndex = 0x13;
        static constexpr std::size_t kUpdateRotationIndex = 0x14;
        static constexpr std::size_t kHandleLookInputIndex = 0x15;

        // Saved orbit (yaw is absolute world yaw, pitch is relative to the actor).
        static inline RE::NiPoint2 g_savedFreeRotation{};
        static inline bool g_savedFreeRotationValid = false;
        // A reseed pins the orbit for a couple of frames while native code settles.
        static inline RE::NiPoint2 g_transitionFreeRotation{};
        static inline float g_transitionAnchorYaw = 0;
        static inline std::uint32_t g_transitionReseedFrames = 0;
        static inline bool g_seededOnThirdPersonBegin = false;
        // Last camera yaw samples, used when entering/leaving ADS and first person.
        static inline float g_lastCameraRootWorldYaw = 0;
        static inline bool g_lastCameraRootWorldYawValid = false;
        static inline float g_lastADSCameraRootWorldYaw = 0;
        static inline bool g_lastADSCameraRootWorldYawValid = false;
        static inline float g_lastFirstPersonCameraYaw = 0;
        static inline float g_lastFirstPersonPitch = 0;
        static inline bool g_lastFirstPersonCameraYawValid = false;
        static inline bool g_wasThirdPerson = false;
        static inline bool g_wasAiming = false;
        // Holster/draw handling.
        static inline bool g_holsterCameraHeld = false;
        static inline bool g_holsteredOrbit = false;
        static inline bool g_drawOrbit = false;
        static inline bool g_lastRealDrawn = false;
        static inline unsigned g_holsterReleaseFrames = 0;
        // Target lock release.
        static inline unsigned g_lockReleaseFrames = 0;
        static inline float g_releaseWorldPitch = 0;
        static inline double g_hipFirePressedAt = 0;
        // Loading screens and VATS.
        static inline bool g_loading = false;
        static inline bool g_resumeAfterLoading = false;
        static inline bool g_loadOrbitValid = false;
        static inline RE::NiPoint2 g_loadRelativeOrbit{};
        static inline bool g_vatsCameraOwned = false;

        static bool CameraContextAvailable() {
            // Window focus gates input, not whether the orbit is kept.
            const auto *ui = RE::UI::GetSingleton();
            return g_gameReady && !Workbench::suspended && !g_loading && !VATSMenuOpen() &&
                   !g_vatsCameraOwned && ui &&
                   (ui->menuMode == 0 || OverlayMenusOnly() || MenuOpen("Console") ||
                    MenuOpen("PauseMenu"));
        }

        // `state` is the active third-person camera.
        static bool IsLiveThirdPerson(const State *state) {
            return IsCurrentCamera(state) && ThirdPersonState() == state;
        }
        // ...and not in iron sights.
        static bool OrbitState(const State *state) {
            return IsLiveThirdPerson(state) && !IronSights(state);
        }

        static bool ReadNodeWorldYaw(const RE::NiAVObject *node, float &yaw) {
            if (!node)
                return false;
            const auto &matrix = node->world.rotation;
            const float x = matrix.rows[0].x, y = matrix.rows[1].x;
            if (!std::isfinite(x) || !std::isfinite(y) || std::hypot(x, y) < 0.00001F)
                return false;
            yaw = Logic::Normalize(std::atan2(y, x));
            return true;
        }
        static bool ReadCameraRootWorldYaw(float &yaw) {
            auto *camera = RE::PlayerCamera::GetSingleton();
            return camera && ReadNodeWorldYaw(camera->cameraRoot.get(), yaw);
        }
        static void RememberCameraYaw(float &yaw, bool &valid) {
            float sample = 0;
            if (ReadCameraRootWorldYaw(sample)) {
                yaw = sample;
                valid = true;
            }
        }
        // World pitch of the rendered view.
        static float ViewPitch() {
            const auto forward = TargetLock::Forward();
            return -std::atan2(forward.z, std::hypot(forward.x, forward.y));
        }

        static void ApplyTransitionReseed(State *state) {
            if (!state || g_transitionReseedFrames == 0)
                return;
            state->targetYaw = g_transitionAnchorYaw;
            state->currentYaw = g_transitionAnchorYaw;
            state->freeRotation = g_transitionFreeRotation;
            g_savedFreeRotation = g_transitionFreeRotation;
            g_savedFreeRotationValid = true;
        }
        static void StartTransitionReseed(State *state, float anchorYaw, RE::NiPoint2 freeRotation) {
            g_transitionAnchorYaw = Logic::Normalize(anchorYaw);
            g_transitionFreeRotation = freeRotation;
            g_transitionReseedFrames = 2;
            ApplyTransitionReseed(state);
        }

        // Hip fire: the body turns to the camera while shooting, then eases back.
        static void ResetHipFire() {
            g_fireBodyValid = false;
            g_returnRemaining = 0;
            g_hipFireUntil = 0;
            g_hipFireWasActive = false;
            g_hipFireReleaseFrames = 0;
        }

        static void RefreshHipFire(RE::PlayerCharacter *player) {
            if (g_cameraWeaponSpoofActive)
                return;
            if (!HipFireContext(player)) {
                ResetHipFire();
                return;
            }
            if (player->gunState == RE::GUN_STATE::kFire)
                g_hipFireUntil = Now() + g_config.hipFireHoldMs / 1000.0;
            // Should the gun relax anyway, stop holding the body to the camera: the
            // relaxed locomotion turns and runs, and a held body makes it run the
            // wrong way. The first moments after a press are the gun coming up.
            if (player->gunState == RE::GUN_STATE::kRelaxed && Now() - g_hipFirePressedAt > 0.35 &&
                g_hipFireUntil > Now())
                g_hipFireUntil = Now();
            const bool firing = HipFireMovementActive(player);
            if (firing) {
                g_hipFireReleaseFrames = 0;
                g_returnRemaining = 0;
            } else if (g_hipFireWasActive) {
                g_hipFireReleaseFrames = 2;
                g_returnYaw = player->data.angle.z;
                g_returnRemaining = g_config.smoothHipFire ? 0.18F : 0;
                g_fireBodyValid = false;
            }
            g_hipFireWasActive = firing;
        }

        static bool HipFireCameraActive(State *state) {
            return IsLiveThirdPerson(state) &&
                   (g_hipFireCameraScoped ||
                    HipFireContext(RE::PlayerCharacter::GetSingleton())) &&
                   (Now() < g_hipFireUntil || g_hipFireReleaseFrames > 0 || g_returnRemaining > 0);
        }

        static void FaceHipFire(RE::PlayerCharacter *player, float delta = 0) {
            // In power armor the movement hook does the turning so native
            // locomotion keeps its root motion.
            if (player && InPowerArmor(player) && g_directionalHooksInstalled.load())
                return;
            if (!HipFireMovementActive(player))
                return;
            auto *third = ActiveThirdPerson();
            if (!third)
                return;
            const float yaw =
                g_savedFreeRotationValid ? g_savedFreeRotation.x : third->freeRotation.x;
            if (!std::isfinite(yaw))
                return;
            if (!g_fireBodyValid) {
                g_fireBodyYaw = player->data.angle.z;
                g_fireBodyValid = true;
            }
            // With torso twist the body keeps close to the move direction (or where
            // it stands) and the spine covers the rest; see TorsoTwist.
            float target = yaw;
            if (TorsoTwist::Enabled()) {
                const auto stick = g_moveStick;
                const bool moving = std::hypot(stick.x, stick.y) >= 0.1F;
                const float wanted = moving ? yaw + std::atan2(stick.x, stick.y) : g_fireBodyYaw;
                target = Twist::BodyTarget(yaw, wanted, g_fireBodyYaw, TorsoTwist::Limit());
            }
            if (TorsoTwist::Enabled())
                g_fireBodyYaw = Twist::TurnBody(g_fireBodyYaw, target, delta);
            else
                g_fireBodyYaw = g_config.smoothHipFire ? Facing::Blend(g_fireBodyYaw, target, delta)
                                                       : Logic::Normalize(target);
            // Yaw only. Forcing a position update here breaks jumping.
            player->data.angle.z = g_fireBodyYaw;
        }

        static void BlendMovementReturn(RE::PlayerCharacter *player, float delta) {
            if (player && InPowerArmor(player) && g_directionalHooksInstalled.load()) {
                g_returnRemaining = 0;
                return;
            }
            if (!player || g_returnRemaining <= 0 || !HipFireContext(player))
                return;
            const float dt = std::isfinite(delta) ? std::clamp(delta, 0.0F, 0.05F) : 0;
            // Blend toward the direction native movement just chose, not the pre-shot facing.
            g_returnYaw = Facing::Blend(g_returnYaw, player->data.angle.z, dt, g_returnRemaining);
            g_returnRemaining = std::max(0.0F, g_returnRemaining - dt);
            player->data.angle.z = g_returnYaw;
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
            // Turn before the attack input is processed; the gun state keeps the
            // window open during sustained fire.
            if (!HipFireMovementActive(player)) {
                g_fireBodyYaw = player->data.angle.z;
                g_fireBodyValid = true;
            }
            g_returnRemaining = 0;
            g_hipFirePressedAt = Now();
            g_hipFireUntil = Now() + g_config.hipFireHoldMs / 1000.0;
            g_hipFireReleaseFrames = 0;
            g_hipFireWasActive = true;
            FaceHipFire(player);
        }

        // Favorites: the camera sees "holstered" while the equip plays out.
        static void BeginFavoriteCamera() {
            ADSTurn::Cancel();
            TargetLock::Unlock();
            ResetHipFire();
            auto *player = RE::PlayerCharacter::GetSingleton();
            // Still the outgoing weapon at this point.
            g_favoriteFromMelee = player && TargetLock::EquippedMelee(player);
            auto *third = ActiveThirdPerson();
            g_favoriteCameraHeld = TTPActive() && Gameplay() && player &&
                                   (WeaponDrawn(player) || g_favoriteCameraHeld) && third &&
                                   !IronSights(third);
        }

        static bool FavoriteCameraActive(State *state) {
            if (!g_favoriteCameraHeld)
                return false;
            if (!FavoriteSwitchInProgress(RE::PlayerCharacter::GetSingleton()) ||
                !TTPActive() || !CameraContextAvailable() || !OrbitState(state)) {
                g_favoriteCameraHeld = false;
                return false;
            }
            return true;
        }

        static void ClearHolster() {
            g_holsterCameraHeld = false;
            g_holsteredOrbit = false;
            g_drawOrbit = false;
            g_holsterReleaseFrames = 0;
        }

        // Runs before the player update, outside any temporary weapon-state spoof.
        static void ObserveHolster() {
            if (g_cameraWeaponSpoofActive)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *third = ThirdPersonState();
            if (!TTPActive() || !CameraContextAvailable() || !player || !OrbitState(third)) {
                ClearHolster();
                g_lastRealDrawn = false;
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
            // Favorites handle their own camera transition.
            if (!g_holsterCameraHeld && !FavoriteCameraActive(third) && g_savedFreeRotationValid &&
                (sheathing || directHolster)) {
                g_holsterCameraHeld = true;
                g_holsterReleaseFrames = 2;
            }
            if (state != RE::WEAPON_STATE::kSheathed && !sheathing) {
                g_holsterCameraHeld = false;
                g_holsteredOrbit = false;
                g_holsterReleaseFrames = 0;
            }
            g_lastRealDrawn = state == RE::WEAPON_STATE::kDrawn;
        }

        static bool HolsterCameraActive(State *state) {
            if (!g_holsterCameraHeld && !g_holsteredOrbit && !g_drawOrbit)
                return false;
            if (!TTPActive() || !CameraContextAvailable() || !OrbitState(state)) {
                ClearHolster();
                return false;
            }
            return true;
        }

        static void FinishHolsterCameraUpdate() {
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (g_holsterCameraHeld && !g_cameraWeaponSpoofActive && player &&
                player->weaponState == RE::WEAPON_STATE::kSheathed && g_holsterReleaseFrames > 0 &&
                --g_holsterReleaseFrames == 0) {
                g_holsterCameraHeld = false;
                g_holsteredOrbit = g_config.keepHolsterView;
            }
        }

        // Evaluated from live state, so POV and menu transitions can't leave an
        // armed camera waiting on g_drawOrbit.
        static bool ArmedCameraActive(State *state) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            return TTPActive() && CameraContextAvailable() && player && OrbitState(state) &&
                   (WeaponDrawn(player) || g_cameraWeaponSpoofActive);
        }

        static bool ProtectedOrbit(State *state) {
            if (!TTPActive() || VATSMenuOpen() || g_vatsCameraOwned)
                return false;
            return ArmedCameraActive(state) || FavoriteCameraActive(state) ||
                   HolsterCameraActive(state) || HipFireCameraActive(state) ||
                   TargetLock::ProtectCamera();
        }

        // For the length of a native camera call, the camera sees a holstered
        // weapon so it keeps free rotation. In power armor the predicate patch
        // does this instead and the real weapon state is left alone.
        struct CameraWeaponScope {
            RE::PlayerCharacter *player = nullptr;
            RE::WEAPON_STATE previousState{};
            bool previousSpoof = g_cameraWeaponSpoofActive;
            RE::WEAPON_STATE previousRealState = g_cameraRealWeaponState;
            bool previousHipFireScope = g_hipFireCameraScoped;
            bool previousLockScope = TargetLock::cameraScope;
            bool previousPowerArmorScope = PowerArmorCamera::scoped;
            explicit CameraWeaponScope(State *state) {
                if (g_nativePOVInput || !ProtectedOrbit(state))
                    return;
                player = RE::PlayerCharacter::GetSingleton();
                if (!player)
                    return;
                if (InPowerArmor(player)) {
                    PowerArmorCamera::scoped = PowerArmorCamera::installed;
                    player = nullptr;
                    return;
                }
                g_hipFireCameraScoped = HipFireCameraActive(state);
                TargetLock::cameraScope = TargetLock::ProtectCamera();
                previousState = player->weaponState;
                if (!previousSpoof)
                    g_cameraRealWeaponState = previousState;
                g_cameraWeaponSpoofActive = true;
                player->weaponState = RE::WEAPON_STATE::kSheathed;
            }
            ~CameraWeaponScope() {
                PowerArmorCamera::scoped = previousPowerArmorScope;
                if (!player)
                    return;
                if (player->weaponState == RE::WEAPON_STATE::kSheathed)
                    player->weaponState = previousState;
                g_cameraWeaponSpoofActive = previousSpoof;
                g_cameraRealWeaponState = previousRealState;
                g_hipFireCameraScoped = previousHipFireScope;
                TargetLock::cameraScope = previousLockScope;
            }
        };

        static bool Active(State *state) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            const bool transitionMode =
                CameraContextAvailable() && TTPActive() && g_transitionReseedFrames > 0 &&
                player && (WeaponDrawn(player) || g_holsteredOrbit) && state &&
                ThirdPersonState() == state;
            // Evaluation order matters: several of these release their own state.
            const bool currentStateActive =
                (IndependentFacingActive(player) || ArmedCameraActive(state) ||
                 FavoriteCameraActive(state) || HolsterCameraActive(state) ||
                 HipFireCameraActive(state) || TargetLock::ProtectCamera()) &&
                IsCurrentCamera(state);
            return CameraContextAvailable() && TTPActive() &&
                   (currentStateActive || transitionMode) && !IronSights(state);
        }

        // Seeds the orbit on entering third person and hands facing over on ADS entry/exit.
        static void SyncAimTransition() {
            if (YieldToVATS())
                return;
            if (!TTPActive()) {
                // Forget the view so switching on seeds the orbit from the current camera.
                g_wasThirdPerson = false;
                g_wasAiming = false;
                return;
            }
            ResumeAfterLoading();
            if (g_loading || g_cameraWeaponSpoofActive)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            auto *thirdPerson = ThirdPersonState();
            const bool inThirdPerson = IsCurrentCamera(thirdPerson);

            if (camera && camera->IsStateActive(RE::CameraStates::kFirstPerson)) {
                g_lastFirstPersonPitch = player ? player->data.angle.x : 0.0F;
                RememberCameraYaw(g_lastFirstPersonCameraYaw, g_lastFirstPersonCameraYawValid);
            } else if (!inThirdPerson) {
                g_lastFirstPersonCameraYawValid = false;
            }

            if (inThirdPerson != g_wasThirdPerson) {
                if (inThirdPerson && player &&
                    (WeaponDrawn(player) || (g_config.keepHolsterView &&
                                             player->weaponState == RE::WEAPON_STATE::kSheathed))) {
                    float cameraWorldYaw = 0.0F;
                    const bool usedFirstPersonYaw = g_lastFirstPersonCameraYawValid;
                    if (usedFirstPersonYaw)
                        cameraWorldYaw = g_lastFirstPersonCameraYaw;
                    else if (!ReadCameraRootWorldYaw(cameraWorldYaw))
                        cameraWorldYaw = player->data.angle.z;

                    const RE::NiPoint2 desired{
                        Logic::SignedYaw(cameraWorldYaw),
                        usedFirstPersonYaw ? g_lastFirstPersonPitch - player->data.angle.x : 0.0F};
                    if (player->weaponState == RE::WEAPON_STATE::kSheathed &&
                        g_config.keepHolsterView)
                        g_holsteredOrbit = true;
                    if (!g_seededOnThirdPersonBegin)
                        StartTransitionReseed(thirdPerson, player->data.angle.z, desired);
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

            const bool aiming =
                player && WeaponDrawn(player) && inThirdPerson && IronSights(thirdPerson);
            // A camera notification can arrive before the actor-state handoff;
            // don't snap facing in the middle of our own pre-turn.
            if (aiming && ADSTurn::pending)
                return;
            if (aiming == g_wasAiming) {
                if (aiming)
                    RememberCameraYaw(g_lastADSCameraRootWorldYaw, g_lastADSCameraRootWorldYawValid);
                return;
            }

            if (aiming) {
                // Entering ADS: face where the camera was looking.
                g_transitionReseedFrames = 0;
                float cameraWorldYaw = 0.0F;
                const bool haveCameraYaw =
                    g_lastCameraRootWorldYawValid || ReadCameraRootWorldYaw(cameraWorldYaw);
                if (g_lastCameraRootWorldYawValid)
                    cameraWorldYaw = g_lastCameraRootWorldYaw;
                if (haveCameraYaw) {
                    player->data.angle.z = cameraWorldYaw;
                    player->Update3DPosition(false);
                }
                g_savedFreeRotationValid = false;
                g_lastCameraRootWorldYawValid = false;
                RememberCameraYaw(g_lastADSCameraRootWorldYaw, g_lastADSCameraRootWorldYawValid);
            } else {
                // Leaving ADS: put the orbit back where the sights were pointing.
                const bool returningToHipFire = player && WeaponDrawn(player) && inThirdPerson;
                float exitCameraYaw = 0.0F;
                const bool haveExitCameraYaw =
                    g_lastADSCameraRootWorldYawValid || ReadCameraRootWorldYaw(exitCameraYaw);
                if (g_lastADSCameraRootWorldYawValid)
                    exitCameraYaw = g_lastADSCameraRootWorldYaw;
                if (returningToHipFire && haveExitCameraYaw) {
                    StartTransitionReseed(thirdPerson, player->data.angle.z,
                                          {Logic::SignedYaw(exitCameraYaw), 0.0F});
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
            if (!g_wasAiming)
                return;
            auto *third = ActiveThirdPerson();
            if (third && !IronSights(third))
                SyncAimTransition();
        }

        // Target lock release: continue free look from the last locked view.
        static void BeginLockRelease() {
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            if (!TTPActive() || !Gameplay() || VATSMenuOpen() || !player || !camera ||
                !camera->IsStateActive(RE::CameraStates::kThirdPerson) || IronSightsActive() ||
                TargetLock::EquippedMode(player) == TargetLock::Mode::None)
                return;
            auto *state = static_cast<State *>(camera->GetState().get());
            if (!state)
                return;
            // Same +Y-forward world convention as lock aiming.
            const auto forward = TargetLock::Forward();
            const float worldPitch = -std::atan2(forward.z, std::hypot(forward.x, forward.y));
            const float worldYaw = std::atan2(forward.x, forward.y);
            if (!std::isfinite(worldYaw) || !std::isfinite(worldPitch))
                return;
            g_releaseWorldPitch = worldPitch;
            g_savedFreeRotation = {Logic::SignedYaw(worldYaw), worldPitch - player->data.angle.x};
            state->freeRotation = g_savedFreeRotation;
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
        }

        static void RebaseLockRelease(State *state) {
            if (!g_lockReleaseFrames)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player || TargetLock::lockRequested ||
                !(HolsterCameraActive(state) || TargetLock::ProtectCamera())) {
                g_lockReleaseFrames = 0;
                return;
            }
            // Independent movement can change actor yaw/pitch right away. Move both
            // interpolation anchors with it and keep the orbit.
            state->targetYaw = player->data.angle.z;
            state->currentYaw = player->data.angle.z;
            g_savedFreeRotation.y = g_releaseWorldPitch - player->data.angle.x;
            state->freeRotation = g_savedFreeRotation;
        }

        // Shared body of the camera callbacks that own the orbit: restore the saved
        // orbit, force free rotation on for the native call, then keep what it produced.
        template <class Native, class BeforeCall, class AfterSave>
        static void RunWithOrbit(State *state, Native &&native, BeforeCall &&beforeCall,
                                 AfterSave &&afterSave, bool consumeReseedFrame) {
            auto *camera = RE::PlayerCamera::GetSingleton();
            if (g_transitionReseedFrames > 0)
                ApplyTransitionReseed(state);
            else if (g_savedFreeRotationValid)
                state->freeRotation = g_savedFreeRotation;
            beforeCall();
            RebaseLockRelease(state);
            RememberCameraYaw(g_lastCameraRootWorldYaw, g_lastCameraRootWorldYawValid);

            const bool beforeEnabled = state->freeRotationEnabled;
            const bool beforeReady = camera->freeRotationReady;
            state->freeRotationEnabled = true;
            camera->freeRotationReady = true;
            {
                CameraWeaponScope weaponScope(state);
                native();
            }
            if (g_transitionReseedFrames > 0) {
                ApplyTransitionReseed(state);
                if (consumeReseedFrame)
                    --g_transitionReseedFrames;
            } else {
                g_savedFreeRotation = state->freeRotation;
                g_savedFreeRotationValid = true;
                afterSave();
            }
            if (Active(state))
                RememberCameraYaw(g_lastCameraRootWorldYaw, g_lastCameraRootWorldYawValid);
            state->freeRotationEnabled = beforeEnabled;
            camera->freeRotationReady = beforeReady;
        }

        // vtable hooks on ThirdPersonState

        static void BeginThunk(State *self) {
            Profile::Scope timing;
            if (YieldToVATS() || Workbench::suspended) {
                Profile::Call(BeginFunc, self);
                return;
            }
            // Sample the outgoing first-person view before native Begin resets it.
            // Only a real first-person sample counts, never a loading/cinematic view.
            if (TTPActive() && Gameplay() && g_lastFirstPersonCameraYawValid) {
                float yaw = 0;
                if (ReadCameraRootWorldYaw(yaw))
                    g_lastFirstPersonCameraYaw = yaw;
                if (auto *p = RE::PlayerCharacter::GetSingleton())
                    g_lastFirstPersonPitch = p->data.angle.x;
            }
            Profile::Call(BeginFunc, self);

            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!TTPActive() || !Gameplay() || !player ||
                (!WeaponDrawn(player) && !(g_config.keepHolsterView &&
                                           player->weaponState == RE::WEAPON_STATE::kSheathed)) ||
                IronSights(self) || !g_lastFirstPersonCameraYawValid) {
                g_seededOnThirdPersonBegin = false;
                return;
            }
            if (player->weaponState == RE::WEAPON_STATE::kSheathed && g_config.keepHolsterView)
                g_holsteredOrbit = true;
            StartTransitionReseed(self, player->data.angle.z,
                                  {Logic::SignedYaw(g_lastFirstPersonCameraYaw),
                                   g_lastFirstPersonPitch - player->data.angle.x});
            g_seededOnThirdPersonBegin = true;
        }

        // A game menu stopped the orbit while the third-person camera kept running.
        // The native camera may have moved the orbit in between, so restart it
        // from what is on screen instead of from the stored rotation.
        static inline bool g_menuDropped = false;
        static void NoteMenuDrop(State *state) {
            const auto *ui = RE::UI::GetSingleton();
            if (TTPActive() && ui && ui->menuMode != 0 && !g_loading && !VATSMenuOpen() &&
                !Workbench::suspended && IsLiveThirdPerson(state) && !g_menuDropped) {
                g_menuDropped = true;
                TTPDiagnostic::Trace("Menu: orbit paused");
            }
        }
        static void ResumeAfterMenu(State *state) {
            if (!g_menuDropped)
                return;
            g_menuDropped = false;
            auto *player = RE::PlayerCharacter::GetSingleton();
            float yaw = 0, pitch = 0;
            if (player && g_transitionReseedFrames == 0 && !IronSights(state) &&
                ReadView(yaw, pitch)) {
                ReseedFromView(state, player, yaw, pitch);
                TTPDiagnostic::Trace("Menu: orbit resumed from current view");
            }
        }

        static void UpdateThunk(State *self, RE::BSTSmartPointer<RE::TESCameraState> &nextState) {
            Profile::Scope timing;
            if (YieldToVATS()) {
                Profile::Call(UpdateFunc, self, nextState);
                return;
            }
            if (Workbench::suspended) {
                Profile::Call(UpdateFunc, self, nextState);
                ResumeWorkbench(self);
                return;
            }
            ResumeAfterLoading();
            SyncAimReleaseBeforeCameraUpdate();
            if (!Active(self)) {
                g_savedFreeRotationValid = false;
                NoteMenuDrop(self);
                Profile::Call(UpdateFunc, self, nextState);
                WaterWeaponVisibility::Update(RE::PlayerCharacter::GetSingleton());
                return;
            }
            ResumeAfterMenu(self);
            RunWithOrbit(
                self, [&] { Profile::Call(UpdateFunc, self, nextState); },
                [&] {
                    if (TargetLock::lockRequested) {
                        TargetLock::AimCamera(self);
                        g_savedFreeRotation = self->freeRotation;
                        g_savedFreeRotationValid = true;
                    }
                },
                [] {}, true);
            if (g_lockReleaseFrames)
                --g_lockReleaseFrames;
            FinishHolsterCameraUpdate();
            WaterWeaponVisibility::Update(RE::PlayerCharacter::GetSingleton());
            if (g_hipFireReleaseFrames > 0)
                --g_hipFireReleaseFrames;
        }

        // Chained so Commonwealth Camera's look input still works.
        static void HandleLookInputThunk(State *self, const RE::NiPoint2 &rawInput) {
            Profile::Scope timing;
            if (YieldToVATS() || Workbench::suspended || !TTPActive()) {
                Profile::Call(HandleLookInputFunc, self, rawInput);
                return;
            }
            RE::NiPoint2 input = rawInput;
            if (TTPActive() && Gameplay()) {
                const auto *player = RE::PlayerCharacter::GetSingleton();
                const bool aiming = IronSights(self) ||
                                    (player && (player->gunState == RE::GUN_STATE::kSighted ||
                                                player->gunState == RE::GUN_STATE::kFireSighted));
                const auto &profile = LookDevice::controller ? g_config.controllerSensitivity
                                                             : g_config.mouseSensitivity;
                const auto &axes = profile.Select(aiming, LookDevice::armed);
                input.x *= axes.x;
                input.y *= axes.y;
            }
            if (g_loading || g_resumeAfterLoading || !GameFocused() || MenuOpen("Console") ||
                MenuOpen("PauseMenu"))
                return;
            if (TargetLock::Active())
                return;
            ResumeAfterLoading();
            SyncAimReleaseBeforeCameraUpdate();
            if (!Active(self)) {
                g_savedFreeRotationValid = false;
                Profile::Call(HandleLookInputFunc, self, input);
                return;
            }
            RunWithOrbit(
                self, [&] { Profile::Call(HandleLookInputFunc, self, input); }, [] {},
                [&] {
                    if (g_lockReleaseFrames)
                        if (auto *player = RE::PlayerCharacter::GetSingleton())
                            g_releaseWorldPitch = player->data.angle.x + self->freeRotation.y;
                },
                false);
        }

        static void UpdateRotationThunk(State *self) {
            Profile::Scope timing;
            if (YieldToVATS() || Workbench::suspended) {
                Profile::Call(UpdateRotationFunc, self);
                return;
            }
            ResumeAfterLoading();
            SyncAimReleaseBeforeCameraUpdate();
            if (!Active(self)) {
                g_savedFreeRotationValid = false;
                Profile::Call(UpdateRotationFunc, self);
                return;
            }
            RunWithOrbit(self, [&] { Profile::Call(UpdateRotationFunc, self); }, [] {}, [] {}, false);
        }

        // Native code picks the shoulder offset here: fOverShoulderPos* holstered,
        // fOverShoulderCombat* (guns) or fOverShoulderMeleeCombat* drawn. Leaving ADS
        // calls this too, with the drawn flag read from the actor. Inside
        // CameraWeaponScope the actor reads "holstered", so the real state is used
        // on every path; otherwise the holstered offset stuck after ADS.
        static void WeaponDrawnChangeThunk(State *state, bool drawn) {
            Profile::Scope timing;
            const bool realDrawn =
                g_cameraWeaponSpoofActive
                    ? std::to_underlying(g_cameraRealWeaponState) >=
                          std::to_underlying(RE::WEAPON_STATE::kDrawn)
                    : drawn;
            ChangeWeaponDrawn(state, drawn, realDrawn);
            AddShoulderOffset(state, realDrawn);
        }

        static void ChangeWeaponDrawn(State *state, bool drawn, bool realDrawn) {
            if (YieldToVATS() || Workbench::suspended) {
                Profile::Call(WeaponDrawnChangeFunc, state, realDrawn);
                return;
            }
            if (!drawn && !g_cameraWeaponSpoofActive && TargetLock::lockRequested && Gameplay()) {
                TargetLock::Unlock();
                if (g_lockReleaseFrames && g_savedFreeRotationValid) {
                    g_holsterCameraHeld = true;
                    g_holsterReleaseFrames = 2;
                    g_holsteredOrbit = g_config.keepHolsterView;
                }
            }
            if (!ProtectedOrbit(state)) {
                Profile::Call(WeaponDrawnChangeFunc, state, realDrawn);
                return;
            }
            // Orbit and yaw are kept across the native call.
            const auto orbit = g_savedFreeRotationValid ? g_savedFreeRotation : state->freeRotation;
            const float anchor = state->targetYaw, current = state->currentYaw;
            Profile::Call(WeaponDrawnChangeFunc, state, realDrawn);
            state->freeRotation = orbit;
            state->targetYaw = anchor;
            state->currentYaw = current;
            g_savedFreeRotation = orbit;
            g_savedFreeRotationValid = true;
        }

        // TTP's camera offsets go on top of whatever the game's settings give (the
        // INI, or another camera mod writing them), so they add up instead of one
        // overriding the other. This is the only place the game sets the offset
        // outside ADS, so it is added exactly once per change.
        static void AddShoulderOffset(State *state, bool drawn) {
            if (!state)
                return;
            auto *player = RE::PlayerCharacter::GetSingleton();
            const auto &offset =
                !drawn ? g_config.holsteredOffset
                : player && TargetLock::EquippedMode(player) == TargetLock::Mode::Ranged
                    ? g_config.rangedOffset
                    : g_config.meleeOffset;
            state->targetShoulderOffset.x += offset.x;
            state->targetShoulderOffset.y += offset.y;
            state->targetShoulderOffset.z += offset.z;
        }

        // After the offsets change: let the game pick the offset again (through
        // the hook above, which adds the new values).
        static void RefreshShoulderOffset() {
            auto *state = ActiveThirdPerson();
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (state && player && !IronSights(state))
                state->ProcessWeaponDrawnChange(WeaponDrawn(player));
        }

        static void SetFreeRotationModeThunk(State *self, bool cameraEnable, bool modifyRotation) {
            Profile::Scope timing;
            if (YieldToVATS() || Workbench::suspended) {
                Profile::Call(SetFreeRotationModeFunc, self, cameraEnable, modifyRotation);
                return;
            }
            if (ProtectedOrbit(self))
                return;
            Profile::Call(SetFreeRotationModeFunc, self, cameraEnable, modifyRotation);
            if (g_transitionReseedFrames > 0 && Active(self))
                ApplyTransitionReseed(self);
        }

        // Only installed with Reload Fix, which wraps this slot. Its handler runs
        // with the real (drawn) weapon state; our orbit is kept if the camera
        // stays in third person afterwards.
        static void ButtonInputThunk(State *state, const RE::ButtonEvent *event) {
            Profile::Scope timing;
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            if (!state || !event || !player || !camera || !TTPActive() || !Gameplay()) {
                Profile::Call(ButtonInputFunc, state, event);
                return;
            }
            const auto realWeapon =
                g_cameraWeaponSpoofActive ? g_cameraRealWeaponState : player->weaponState;
            if (realWeapon != RE::WEAPON_STATE::kDrawn || IronSights(state) ||
                !camera->IsStateActive(RE::CameraStates::kThirdPerson)) {
                Profile::Call(ButtonInputFunc, state, event);
                return;
            }
            const std::string_view action = event->QUserEvent().c_str();
            const bool pov = action == "TogglePOV" || action == "ZoomIn" || action == "ZoomOut";
            const auto orbit = state->freeRotation;
            const bool beforeFree = state->freeRotationEnabled;
            WeaponInputScope inputScope(*player, g_cameraWeaponSpoofActive, g_nativePOVInput,
                                        realWeapon, pov);
            // With this false, Reload Fix zeroes yaw on release; vanilla POV then
            // compares that zero against the orbit saved on press and treats the
            // press as an orbit drag. Reload Fix still decides everything else.
            state->freeRotationEnabled = true;
            Profile::Call(ButtonInputFunc, state, event);
            if (camera->GetState().get() == state && !IronSights(state)) {
                state->freeRotationEnabled = beforeFree;
                state->freeRotation = orbit;
                g_savedFreeRotation = orbit;
                g_savedFreeRotationValid = true;
            }
        }

        static bool Install() {
            if (g_cameraHooksInstalled.load(std::memory_order_acquire))
                return true;
            auto *thirdPerson = ThirdPersonState();
            if (!thirdPerson)
                return false;
            auto **vtable = *reinterpret_cast<void ***>(thirdPerson);
            if (!vtable || !vtable[kBeginIndex] || !vtable[kUpdateIndex] ||
                !vtable[kWeaponDrawnChangeIndex] || !vtable[kSetFreeRotationModeIndex] ||
                !vtable[kUpdateRotationIndex] || !vtable[kHandleLookInputIndex]) {
                REX::LogError("ThirdPersonState camera vtable entries were null");
                return false;
            }
            HookBatch hooks(reinterpret_cast<std::uintptr_t *>(vtable));
            const bool reloadFix =
                GetModuleHandleW(L"ReloadFix.dll") || GetModuleHandleW(L"ReloadFixAE.dll");
            if (reloadFix)
                hooks.Add(kButtonInputIndex, ButtonInputThunk, ButtonInputFunc);
            hooks.Add(kBeginIndex, BeginThunk, BeginFunc);
            hooks.Add(kUpdateIndex, UpdateThunk, UpdateFunc);
            hooks.Add(kWeaponDrawnChangeIndex, WeaponDrawnChangeThunk, WeaponDrawnChangeFunc);
            hooks.Add(kSetFreeRotationModeIndex, SetFreeRotationModeThunk, SetFreeRotationModeFunc);
            hooks.Add(kUpdateRotationIndex, UpdateRotationThunk, UpdateRotationFunc);
            hooks.Add(kHandleLookInputIndex, HandleLookInputThunk, HandleLookInputFunc);
            if (!hooks.Commit())
                return false;
            if (reloadFix)
                TTPDiagnostic::Trace("Reload Fix detected: POV input chained");
            g_cameraHooksInstalled.store(true, std::memory_order_release);
            return true;
        }

        // Loading screens: keep the orbit relative to the actor and restore it on arrival.
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
            auto *state = ThirdPersonState();
            const bool preserve = g_gameReady && TTPActive() && player && camera && state &&
                                  camera->IsStateActive(RE::CameraStates::kThirdPerson) &&
                                  !IronSights(state) && g_savedFreeRotationValid;
            RE::NiPoint2 relative{};
            if (preserve)
                relative = {Logic::SignedYaw(g_savedFreeRotation.x - player->data.angle.z),
                            g_savedFreeRotation.y};
            ADSTurn::Cancel();
            Reset();
            g_loading = true;
            g_loadOrbitValid = preserve;
            g_loadRelativeOrbit = relative;
            TTPDiagnostic::Trace("Loading: camera state cleared");
        }

        // Orbit flags that follow from the current weapon state after a resume.
        static void AdoptWeaponState(RE::PlayerCharacter *player) {
            g_holsteredOrbit = g_config.keepHolsterView && !WeaponDrawn(player);
            g_drawOrbit = WeaponDrawn(player);
            g_lastRealDrawn = WeaponDrawn(player);
            g_wasThirdPerson = true;
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
            auto *state = ThirdPersonState();
            if (!state)
                return;
            g_resumeAfterLoading = false;
            g_loadOrbitValid = false;
            if (IronSights(state))
                return;
            AdoptWeaponState(player);
            StartTransitionReseed(
                state, player->data.angle.z,
                {Logic::SignedYaw(player->data.angle.z + g_loadRelativeOrbit.x),
                 g_loadRelativeOrbit.y});
            TTPDiagnostic::Trace("Loading: orbit restored relative to arrival facing");
        }

        // Restart the orbit from what is on screen now. False if the view can't be read yet.
        static bool ReadView(float &yaw, float &pitch) {
            pitch = ViewPitch();
            return ReadCameraRootWorldYaw(yaw) && std::isfinite(pitch);
        }
        static void ReseedFromView(State *state, RE::PlayerCharacter *player, float yaw,
                                   float pitch) {
            StartTransitionReseed(state, player->data.angle.z,
                                  {Logic::SignedYaw(yaw), pitch - player->data.angle.x});
        }
        static bool ResumeFromCurrentView(State *state, RE::PlayerCharacter *player) {
            float yaw = 0, pitch = 0;
            if (!ReadView(yaw, pitch))
                return false;
            Reset();
            AdoptWeaponState(player);
            if (TTPActive() && !IronSights(state))
                ReseedFromView(state, player, yaw, pitch);
            return true;
        }

        static void ResumeWorkbench(State *state) {
            const auto *ui = RE::UI::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!Workbench::resumePending || Workbench::Open() || !ui || ui->menuMode != 0 ||
                !GameFocused() || !camera || !player ||
                !camera->IsStateActive(RE::CameraStates::kThirdPerson) ||
                !ResumeFromCurrentView(state, player))
                return;
            Workbench::resumePending = false;
            Workbench::suspended = false;
            TTPDiagnostic::Trace("Workbench: resumed");
        }

        // VATS owns the camera while open. Returns true while TTP should stay out.
        static bool YieldToVATS() {
            if (VATSMenuOpen()) {
                if (!g_vatsCameraOwned) {
                    Reset();
                    ADSTurn::Cancel();
                    g_vatsCameraOwned = true;
                    TTPDiagnostic::Trace("VATS: camera handed to the game");
                }
                return true;
            }
            if (!g_vatsCameraOwned)
                return false;
            // Wait for gameplay before taking the orbit back after menu transitions.
            if (!Gameplay())
                return true;
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            auto *state = ThirdPersonState();
            if (!player || !camera)
                return true;
            if (!OrbitState(state)) {
                g_vatsCameraOwned = false;
                return false;
            }
            if (!ResumeFromCurrentView(state, player))
                return true;
            TTPDiagnostic::Trace("VATS: camera resumed from current view");
            return false;
        }

        static void Reset(bool keepLock = false) {
            g_vatsCameraOwned = false;
            g_loading = false;
            g_resumeAfterLoading = false;
            g_loadOrbitValid = false;
            if (keepLock)
                TargetLock::orbitHeld = false;
            else
                TargetLock::Reset();
            g_lockReleaseFrames = 0;
            ResetHipFire();
            ClearHolster();
            g_lastRealDrawn = false;
            g_favoriteCameraHeld = false;
            g_savedFreeRotationValid = false;
            g_lastCameraRootWorldYawValid = false;
            g_lastADSCameraRootWorldYawValid = false;
            g_lastFirstPersonCameraYawValid = false;
            g_transitionReseedFrames = 0;
            g_seededOnThirdPersonBegin = false;
            g_wasThirdPerson = false;
            g_wasAiming = false;
            g_menuDropped = false;
        }

        static inline decltype(&BeginThunk) BeginFunc = nullptr;
        static inline decltype(&UpdateThunk) UpdateFunc = nullptr;
        static inline decltype(&WeaponDrawnChangeThunk) WeaponDrawnChangeFunc = nullptr;
        static inline decltype(&SetFreeRotationModeThunk) SetFreeRotationModeFunc = nullptr;
        static inline decltype(&UpdateRotationThunk) UpdateRotationFunc = nullptr;
        static inline decltype(&HandleLookInputThunk) HandleLookInputFunc = nullptr;
        static inline decltype(&ButtonInputThunk) ButtonInputFunc = nullptr;
    };
}
