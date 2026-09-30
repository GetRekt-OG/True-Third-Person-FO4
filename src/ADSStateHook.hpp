#pragma once

namespace TrueThirdPerson {
    // Holds native ADS back until the body has turned to the camera heading.
    struct ADSStateHook {
        static inline bool (*original)(RE::ActorState *, bool) = nullptr;
        static bool Eligible(RE::PlayerCharacter *player) {
            if (!player || !g_config.adsTurnMs || !IndependentFacingActive(player) ||
                player->lifeState != RE::ACTOR_LIFE_STATE::kAlive || player->swimming ||
                player->weaponState != RE::WEAPON_STATE::kDrawn || Now() < EquipGuard::until ||
                TargetLock::EquippedMelee(player))
                return false;
            auto *camera = RE::PlayerCamera::GetSingleton();
            return camera && camera->IsStateActive(RE::CameraStates::kThirdPerson) &&
                   GunModelReady(player);
        }
        static bool Thunk(RE::ActorState *self, bool sighted) {
            Profile::Scope timing;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player || self != static_cast<RE::ActorState *>(player))
                return Profile::Call(original, self, sighted);
            if (!sighted) {
                if (ADSTurn::pending)
                    TTPDiagnostic::Trace("ADS engine request cancelled");
                ADSTurn::Cancel();
                return Profile::Call(original, self, false);
            }
            if (!Eligible(player)) {
                ADSTurn::Cancel();
                return Profile::Call(original, self, true);
            }
            if (!ADSTurn::pending) {
                if (player->gunState == RE::GUN_STATE::kSighted ||
                    player->gunState == RE::GUN_STATE::kFireSighted)
                    return Profile::Call(original, self, true);
                CameraOrbit::ResetHipFire();
                ADSTurn::Begin(player->data.angle.z, g_config.adsTurnMs / 1000.0F);
                TTPDiagnostic::Trace("ADS engine request: pre-turn started");
            }
            // Not in sights yet. Tick() calls the original setter once the turn is done.
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
            if (!CameraOrbit::ReadCameraRootWorldYaw(target)) {
                ADSTurn::Cancel();
                return;
            }
            const bool done = ADSTurn::Step(target, dt);
            player->data.angle.z = ADSTurn::yaw;
            if (done) {
                ADSTurn::Cancel();
                const bool entered = Profile::Call(original, static_cast<RE::ActorState *>(player), true);
                TTPDiagnostic::Trace(entered ? "ADS engine handoff accepted"
                                             : "ADS engine handoff rejected");
            }
        }
        static bool Install() {
            // Toggle Aim only latches on release if native ADS is already active,
            // so with it installed the ADS transition stays synchronous.
            if (F4SE::GetPluginInfo("fo4ta03") || GetModuleHandleW(L"fo4ta03.dll")) {
                ADSTurn::Cancel();
                TTPDiagnostic::Trace("Toggle Aim detected: ADS pre-turn disabled");
                return true;
            }
            return original || HookSingle(static_cast<RE::ActorState *>(
                                              RE::PlayerCharacter::GetSingleton()),
                                          0x25, Thunk, original);
        }
    };
}
