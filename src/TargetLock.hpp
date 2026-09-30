#pragma once
#include "Input.hpp"
#include "RE/B/BGSInventoryList.hpp"
#include "RE/B/BGSInventoryItem.hpp"
#include "RE/B/BGSBodyPart.hpp"
#include "RE/B/BGSBodyPartData.hpp"
#include "RE/B/BSUtilities.hpp"
#include "RE/B/BS_BUTTON_CODE.hpp"
#include "RE/N/NiQuaternion.hpp"
#include "RE/T/TESCameraState.hpp"
#include "RE/P/ProcessLists.hpp"
#include "RE/T/TESObjectWEAP.hpp"
#include "RE/T/TESRace.hpp"
#include <shared_mutex>

// Targeting and world-to-screen patterns adapted from CythiaSu/SimpleAimAssist
// (see THIRD-PARTY/SimpleAimAssist-LICENSE.md). True Directional Movement was
// used as a behavior reference only.
namespace TrueThirdPerson::TargetLock {
    // Which lock settings apply: decided by the equipped weapon.
    enum class Mode : std::uint8_t { None, Melee, Ranged };

    inline RE::ActorHandle target;
    inline Mode lockedMode = Mode::None;
    inline ControllerInput::Flick stickFlick;
    inline double nextSwitch = 0, lastTick = 0, lostSight = 0, mouseLast = 0;
    inline float mouseTravel = 0, bodyYaw = 0, bodyPitch = 0, frameDelta = 0;
    inline bool ready = false;
    inline bool orbitHeld = false;
    inline bool lockRequested = false;
    inline void (*onRelease)() = nullptr;
    inline thread_local bool cameraScope = false;
    inline bool Locked() {
        return static_cast<bool>(target.get());
    }
    inline void Unlock(bool keepView = true) {
        if (keepView && lockRequested && onRelease)
            onRelease();
        if (Locked())
            TTPDiagnostic::Trace(lockedMode == Mode::Ranged ? "Ranged target lock released"
                                                            : "Melee target lock released");
        target = RE::ActorHandle{};
        lockRequested = false;
        lockedMode = Mode::None;
        stickFlick.Reset();
        mouseTravel = 0;
        lostSight = 0;
    }
    // Melee only for an equipped melee weapon, ranged for anything else that
    // shoots. Don't infer the type from a missing gun model; equip transitions
    // can hide it.
    struct Equipped {
        Mode mode = Mode::None;
        const RE::TESForm *weapon = nullptr;
    };
    inline Equipped ScanEquipped(RE::PlayerCharacter *p) {
        if (!p || !p->inventoryList)
            return {};
        // Scripts and transfers can change the list from other threads.
        std::shared_lock lock(p->inventoryList->rwLock);
        Equipped equipped;
        for (auto &item : p->inventoryList->data) {
            if (!item.object || item.object->GetFormType() != RE::FormType::kWeapon)
                continue;
            auto *w = static_cast<RE::TESObjectWEAP*>(item.object);
            // Grenades and mines sit in their own equipped slot next to the weapon.
            if (w->weaponData.type == RE::WEAPON_TYPE::kGrenade ||
                w->weaponData.type == RE::WEAPON_TYPE::kMine)
                continue;
            for (auto *stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (!stack->flags.any(RE::BGSInventoryItem::Stack::Flags::kSlotMask))
                    continue;
                const int type = static_cast<int>(w->weaponData.type.underlying());
                if (!LockMath::Melee(type))
                    return {Mode::Ranged, w}; // any ranged weapon wins
                equipped = {Mode::Melee, w};
            }
        }
        return equipped;
    }
    // The scan walks the whole inventory and is asked for on every input event,
    // so reuse the answer for a few milliseconds. Equip changes are seen next frame.
    inline Equipped EquippedWeapon(RE::PlayerCharacter *p) {
        static CachedValue<Equipped> cache;
        if (!p || p != RE::PlayerCharacter::GetSingleton())
            return ScanEquipped(p);
        return cache.Get(0.02, [p] { return ScanEquipped(p); });
    }
    inline Mode EquippedMode(RE::PlayerCharacter *p) {
        return EquippedWeapon(p).mode;
    }
    inline bool EquippedMelee(RE::PlayerCharacter *p) {
        return EquippedMode(p) == Mode::Melee;
    }
    inline const LockSettings &SettingsFor(Mode mode) {
        return mode == Mode::Ranged ? g_config.rangedLock : g_config.meleeLock;
    }
    // The equipped weapon's lock mode, or None when that lock is switched off.
    inline Mode EnabledMode() {
        const Mode mode = EquippedMode(RE::PlayerCharacter::GetSingleton());
        return mode != Mode::None && SettingsFor(mode).enabled ? mode : Mode::None;
    }
    inline const LockSettings &Settings() {
        return SettingsFor(EquippedMode(RE::PlayerCharacter::GetSingleton()));
    }
    // How fast the view follows the target. A gun needs the crosshair to stay on
    // it while either side moves, so ranged tracks the view more tightly.
    inline float Response() {
        return lockedMode == Mode::Ranged ? Settings().response * 2.5F : Settings().response;
    }
    inline bool PauseMenuOpen() {
        static Cached cache;
        return cache.Get(kFrameCache, [] { return MenuOpen("PauseMenu"); });
    }
    inline bool ControlsReserved() {
        auto *p = RE::PlayerCharacter::GetSingleton();
        // Works with the main switch off too ("lock-only"); see Tick().
        return ready && !VATSMenuOpen() && !PauseMenuOpen() && Gameplay() && p &&
               p->lifeState == RE::ACTOR_LIFE_STATE::kAlive &&
               (cameraScope || p->weaponState == RE::WEAPON_STATE::kDrawn) &&
               EnabledMode() != Mode::None;
    }
    // Aiming down sights, or the turn into it.
    inline bool Aiming() {
        const auto *p = RE::PlayerCharacter::GetSingleton();
        return IronSightsActive() || ADSTurn::pending ||
               (p && (p->gunState == RE::GUN_STATE::kSighted ||
                      p->gunState == RE::GUN_STATE::kFireSighted));
    }
    // A ranged lock keeps its target while aiming down sights but doesn't steer.
    inline bool AimPaused() {
        return lockRequested && lockedMode == Mode::Ranged && ControlsReserved() &&
               EnabledMode() == Mode::Ranged && Aiming();
    }
    inline bool Context() {
        auto *c = RE::PlayerCamera::GetSingleton();
        if (!ControlsReserved() || !c || !c->IsStateActive(RE::CameraStates::kThirdPerson))
            return false;
        auto state = c->GetState();
        if (!state || static_cast<RE::ThirdPersonState *>(state.get())->ironSights ||
            Now() < EquipGuard::until)
            return false;
        return EnabledMode() != Mode::Ranged || !Aiming();
    }
    inline void Reset() {
        Unlock(false);
        orbitHeld = false;
    }
    inline void ReleaseInvalidContext() {
        // The pause menu and ranged aiming suspend targeting but keep the lock.
        if (PauseMenuOpen() || AimPaused()) {
            frameDelta = 0;
            lostSight = 0;
            mouseTravel = 0;
            stickFlick.Reset();
            return;
        }
        // Holstering ends the context before the next tick. Let the camera run
        // its release callback before the lock is cleared.
        Unlock();
        orbitHeld = false;
    }
    inline bool ProtectCamera() {
        if (PauseMenuOpen())
            return orbitHeld;
        if (!Context()) {
            ReleaseInvalidContext();
            return false;
        }
        return orbitHeld;
    }
    inline bool Active() {
        if (!Context()) {
            ReleaseInvalidContext();
            return false;
        }
        return Locked();
    }
    inline RE::NiPoint3 Forward() {
        auto *c = RE::PlayerCamera::GetSingleton();
        if (!c || !c->GetState())
            return {0, 1, 0};
        RE::NiQuaternion q{};
        c->GetState()->GetRotation(q);
        const auto f = LockMath::Forward(q.w, q.x, q.y, q.z);
        return {f[0], f[1], f[2]};
    }
    inline RE::NiPoint3 CameraPosition(RE::PlayerCharacter *p) {
        RE::NiPoint3 out{};
        auto *c = RE::PlayerCamera::GetSingleton();
        if (c && c->GetCameraPosition(out, true))
            return out;
        out = p->GetPosition();
        out.z += 100;
        return out;
    }
    inline RE::NiPoint3 Point(RE::Actor *a) {
        // Aim at the race's torso bone rather than head/limb markers.
        if (a->race && a->race->bodyPartData && a->Get3D()) {
            auto *part = a->race->bodyPartData->partArray[0];
            if (part && !part->targetName.empty()) {
                if (auto *node =
                        RE::BSUtilities::GetObjectByName(a->Get3D(), part->targetName, true, false))
                    return node->world.translation;
            }
        }
        // Otherwise the model's bound center, which works for creatures too.
        auto *root = a->Get3D();
        if (root && root->worldBound.radius.float32 > 1 && std::isfinite(root->worldBound.center.z))
            return root->worldBound.center;
        auto pos = a->GetPosition();
        pos.z += 65;
        return pos;
    }
    inline bool Valid(RE::PlayerCharacter *p, RE::Actor *a) {
        if (!a || a == p || a->lifeState != RE::ACTOR_LIFE_STATE::kAlive || !a->Get3D() ||
            !a->GetHostileToActor(p))
            return false;
        const auto d = a->GetPosition() - p->GetPosition();
        return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) &&
               std::hypot(d.x, d.y, d.z) < Settings().distance;
    }
    inline bool Visible(RE::PlayerCharacter *p, RE::Actor *a) {
        bool picked = false;
        return p->HasLOSToTarget(a, picked);
    }
    inline bool Select(int direction, bool nearest = false) {
        if (!Context())
            return false;
        auto *p = RE::PlayerCharacter::GetSingleton();
        auto *lists = RE::ProcessLists::GetSingleton();
        if (!lists)
            return false;
        auto current = target.get();
        const auto origin = CameraPosition(p), forward = Forward();
        const float viewYaw = std::atan2(forward.x, forward.y);
        float anchor = 0;
        if (!nearest && current && Valid(p, current.get())) {
            auto d = Point(current.get()) - origin;
            anchor = Logic::SignedYaw(std::atan2(d.x, d.y) - viewYaw);
        }
        struct Candidate {
            RE::ActorHandle handle;
            float score;
        };
        std::vector<Candidate> candidates;
        auto visit = [&](auto &handles) {
            for (auto &h : handles) {
                auto a = h.get();
                if (!a || a.get() == current.get() || !Valid(p, a.get()))
                    continue;
                const auto d = Point(a.get()) - origin;
                const float yaw = Logic::SignedYaw(std::atan2(d.x, d.y) - viewYaw);
                const auto bodyDistance = a->GetPosition() - p->GetPosition();
                const float score =
                    LockMath::SelectionScore(nearest, yaw, anchor, direction, bodyDistance.x,
                                             bodyDistance.y, bodyDistance.z);
                if (!std::isfinite(score))
                    continue;
                candidates.push_back({h, score});
            }
        };
        visit(lists->highActorHandles);
        visit(lists->middleHighActorHandles);
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto &a, const auto &b) { return a.score < b.score; });
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto a = candidates[i].handle.get();
            if (a && Visible(p, a.get())) {
                target = candidates[i].handle;
                lostSight = 0;
                bodyYaw = p->data.angle.z;
                bodyPitch = p->data.angle.x;
                orbitHeld = true;
                lockRequested = true;
                lockedMode = EnabledMode();
                REX::LogInformation("Target lock acquired form {:08X}", a->formID);
                TTPDiagnostic::Trace(nearest ? "Automatic target handoff; preserving orbit"
                                             : "Target lock acquired/switched");
                return true;
            }
        }
        if (!direction)
            TTPDiagnostic::Trace("No eligible visible target");
        return false;
    }
    inline bool EnsureTarget() {
        if (!Context()) {
            ReleaseInvalidContext();
            return false;
        }
        if (!lockRequested)
            return false;
        if (lockedMode != EnabledMode()) {
            Unlock(); // weapon type changed under the lock
            return false;
        }
        auto *p = RE::PlayerCharacter::GetSingleton();
        auto a = target.get();
        if (a && Valid(p, a.get()))
            return true;
        // Target died or left range: hand off to the nearest enemy without
        // dropping back to native facing in between.
        if (Select(0, true))
            return true;
        Unlock(); // orbitHeld stays set, so free look continues from the same orbit
        return false;
    }
    inline void Tick(RE::PlayerCharacter *p, float dt) {
        frameDelta = std::isfinite(dt) ? std::clamp(dt, 0.0F, 0.05F) : 0;
        if (!EnsureTarget())
            return;
        auto a = target.get();
        if (!a)
            return;
        const double now = Now();
        if (now >= lastTick) {
            lastTick = now + 0.1;
            if (Visible(p, a.get()))
                lostSight = 0;
            else if (!lostSight)
                lostSight = now;
        }
        if (lostSight && now - lostSight > 0.6) {
            Unlock();
            return;
        }
        const bool ranged = lockedMode == Mode::Ranged;
        if (!TTPActive()) {
            // Lock-only: the vanilla drawn-weapon camera follows the character, so
            // steer the character by what the camera sees. Melee turns the body
            // straight at the target; ranged turns until the crosshair is on it
            // (the shoulder offset makes those differ).
            const auto view = Point(a.get()) - CameraPosition(p);
            const auto f = Forward();
            const auto correction = LockMath::CameraCorrection(
                view.x, view.y, view.z, f.x, f.y, f.z, Response(), frameDelta);
            bodyPitch = std::clamp(p->data.angle.x + correction[1], -1.2F, 1.2F);
            p->data.angle.x = bodyPitch;
            if (ranged) {
                bodyYaw = p->data.angle.z + correction[0];
                p->data.angle.z = bodyYaw;
                return;
            }
        } else if (ranged) {
            bodyPitch = p->data.angle.x; // set from the view in AimCamera
        }
        const auto d = Point(a.get()) - p->GetPosition();
        bodyYaw = LockMath::Turn(p->data.angle.z, std::atan2(d.x, d.y), Settings().response,
                                 frameDelta);
        p->data.angle.z = bodyYaw; // the native update handles position
    }
    inline void RestoreFacing(RE::PlayerCharacter *p) {
        if (p && Active()) {
            p->data.angle.z = bodyYaw;
            if (!TTPActive() || lockedMode == Mode::Ranged)
                p->data.angle.x = bodyPitch;
        }
    }
    inline void AimCamera(RE::ThirdPersonState *state) {
        if (!EnsureTarget())
            return;
        auto a = target.get();
        auto *p = RE::PlayerCharacter::GetSingleton();
        if (!a)
            return;
        const auto d = Point(a.get()) - CameraPosition(p);
        const auto f = Forward();
        // Adjust the orbit, not actor pitch.
        const auto correction =
            LockMath::CameraCorrection(d.x, d.y, d.z, f.x, f.y, f.z, Response(), frameDelta);
        state->freeRotation.x = Logic::SignedYaw(state->freeRotation.x + correction[0]);
        state->freeRotation.y = std::clamp(state->freeRotation.y + correction[1], -1.2F, 1.2F);
        if (lockedMode == Mode::Ranged) {
            // A gun aims with the actor's pitch. Carry the view pitch on the actor so
            // the weapon points where the crosshair is; the view itself doesn't move.
            const float pitch = std::clamp(p->data.angle.x + state->freeRotation.y, -1.2F, 1.2F);
            state->freeRotation.y -= pitch - p->data.angle.x;
            p->data.angle.x = bodyPitch = pitch;
        }
    }
    inline int MouseButton(const RE::InputEvent *event) {
        auto *b = event->As<RE::ButtonEvent>();
        if (!b)
            return -1;
        return LockMath::MouseControl(event->device == RE::INPUT_DEVICE::kMouse, b->QIDCode(),
                                      b->QUserEvent().c_str());
    }
    inline bool ControllerContext() {
        auto *camera = RE::PlayerCamera::GetSingleton();
        return camera && camera->IsStateActive(RE::CameraStates::kThirdPerson);
    }
    // The lock key chosen for the equipped weapon type. Keyboard and controller
    // keys only count in third person; a mouse key is taken whenever the lock
    // owns the controls, as the middle button always was.
    inline bool LockKey(const RE::InputEvent *e) {
        const auto *button = e->As<RE::ButtonEvent>();
        if (!button)
            return false;
        using LockKeys::Device;
        const auto &settings = Settings();
        const auto code = button->QIDCode();
        switch (e->device.get()) {
        case RE::INPUT_DEVICE::kMouse:
            return LockKeys::Matches(LockKeys::Pick(LockKeys::keyboardMouse, settings.key),
                                     Device::Mouse, code);
        case RE::INPUT_DEVICE::kKeyboard:
            return ControllerContext() &&
                   LockKeys::Matches(LockKeys::Pick(LockKeys::keyboardMouse, settings.key),
                                     Device::Keyboard, code);
        case RE::INPUT_DEVICE::kGamepad:
            return ControllerContext() &&
                   LockKeys::Matches(LockKeys::Pick(LockKeys::gamepad, settings.gamepadKey),
                                     Device::Gamepad, code);
        default:
            return false;
        }
    }
    inline const RE::ThumbstickEvent *LookStick(const RE::InputEvent *e) {
        const auto *stick = e->As<RE::ThumbstickEvent>();
        return stick && ControllerInput::LookStick(
            e->device == RE::INPUT_DEVICE::kGamepad, stick->QIDCode()) ? stick : nullptr;
    }
    // Used by both input receivers. Only Input() toggles the lock.
    inline bool Consume(const RE::InputEvent *e) {
        if (!ControlsReserved())
            return false;
        if (LockKey(e))
            return true;
        if (Active() && LookStick(e))
            return true;
        // While unlocked, the wheel still reaches vanilla zoom/POV.
        const int code = MouseButton(e);
        return Active() && (code == 8 || code == 9 || e->As<RE::MouseMoveEvent>());
    }
    inline bool Input(const RE::InputEvent *e) {
        if (PauseMenuOpen())
            return false;
        if (!ControlsReserved()) {
            Unlock();
            return false;
        }
        const bool consume = Consume(e);
        if (!Context()) {
            if (!AimPaused())
                Unlock();
            return consume;
        }
        const int code = MouseButton(e);
        if (auto *b = e->As<RE::ButtonEvent>()) {
            if (LockKey(e) && b->QJustPressed()) {
                if (Locked())
                    Unlock();
                else {
                    Select(0);
                    stickFlick.Reset();
                }
                nextSwitch = Now() + 0.30;
            } else if (Locked() && (code == 8 || code == 9) && b->QPressed() &&
                       Now() >= nextSwitch) {
                Select(code == 8 ? -1 : 1);
                nextSwitch = Now() + 0.25;
                mouseTravel = 0;
            }
        }
        if (auto *m = e->As<RE::MouseMoveEvent>(); m && Locked()) {
            if (Now() >= nextSwitch) {
                if (Now() - mouseLast > 0.15)
                    mouseTravel = 0;
                mouseLast = Now();
                mouseTravel += static_cast<float>(m->mouseInputX);
                if (std::abs(mouseTravel) >= Settings().switchPixels) {
                    Select(mouseTravel > 0 ? 1 : -1);
                    mouseTravel = 0;
                    nextSwitch = Now() + 0.30;
                }
            } else
                mouseTravel = 0;
        }
        if (const auto *stick = LookStick(e)) {
            const int direction = stickFlick.Update(stick->xValue, stick->yValue,
                                                   Locked(), Now() >= nextSwitch);
            if (direction) {
                Select(direction);
                nextSwitch = Now() + 0.30;
            }
        }
        return consume;
    }
    struct CameraInputHook {
        static inline void (*original)(RE::BSInputEventReceiver *,
                                       const RE::InputEvent *) = nullptr;
        static void Thunk(RE::BSInputEventReceiver *self, const RE::InputEvent *head) {
            Profile::Scope timing;
            LookDevice::Observe(head);
            EquipGuard::FilteredQueue queue(head, Consume);
            Profile::Call(original, self, queue.head);
        }
        static bool Install() {
            return original ||
                   HookSingle(static_cast<RE::BSInputEventReceiver *>(
                                  RE::PlayerCamera::GetSingleton()),
                              0, Thunk, original);
        }
    };
}
