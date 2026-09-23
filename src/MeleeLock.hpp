#pragma once
#include "LookDevice.hpp"
#include "CameraSupport.hpp"
#include "ControllerInput.hpp"
#include "RE/T/ThumbstickEvent.hpp"
#include "CombatMath.hpp"
#include "LockMath.hpp"
#include "EquipGuard.hpp"
#include "RE/B/BGSInventoryList.hpp"
#include "RE/B/BGSInventoryItem.hpp"
#include "RE/B/BGSBodyPart.hpp"
#include "RE/B/BGSBodyPartData.hpp"
#include "RE/B/BSUtilities.hpp"
#include "RE/B/BS_BUTTON_CODE.hpp"
#include "RE/M/MouseMoveEvent.hpp"
#include "RE/N/NiQuaternion.hpp"
#include "RE/T/TESCameraState.hpp"
#include "RE/P/ProcessLists.hpp"
#include "RE/T/TESObjectWEAP.hpp"
#include "RE/T/TESRace.hpp"
#include <vector>
#include <string_view>

// Fallout 4 API patterns adapted from CythiaSu/SimpleAimAssist.
// See THIRD-PARTY/SimpleAimAssist-LICENSE.md. TDM is a behavior reference only.
namespace TrueThirdPerson::MeleeLock {
    inline RE::ActorHandle target;
    inline ControllerInput::Flick stickFlick;
    inline double nextSwitch = 0, lastTick = 0, lostSight = 0, mouseLast = 0;
    inline float mouseTravel = 0, bodyYaw = 0, frameDelta = 0;
    inline bool ready = false;
    inline bool orbitHeld = false;
    inline bool lockRequested = false;
    inline void (*onRelease)() = nullptr;
    inline thread_local bool movementScope = false;
    inline thread_local bool cameraScope = false;
    inline double Now() {
        return EquipGuard::Now();
    }
    inline bool Locked() {
        return static_cast<bool>(target.get());
    }
    inline void Unlock(bool keepView = true) {
        if (keepView && lockRequested && onRelease)
            onRelease();
        if (Locked())
            TTPDiagnostic::Trace("Melee target lock released");
        target = RE::ActorHandle{};
        lockRequested = false;
        stickFlick.Reset();
        mouseTravel = 0;
        lostSight = 0;
    }
    // Explicit allowlist: an equipped melee WEAP, including fist weapons. Never
    // infer melee from absence of the gun biped slot; equip transitions can hide it.
    inline bool EquippedMelee(RE::PlayerCharacter *p) {
        if (!p || !p->inventoryList)
            return false;
        bool melee = false;
        for (auto &item : p->inventoryList->data) {
            auto *w = item.object ? item.object->As<RE::TESObjectWEAP>() : nullptr;
            if (!w)
                continue;
            // Thrown weapons occupy a separate equipped slot beside the main weapon.
            if (w->weaponData.type == RE::WEAPON_TYPE::kGrenade ||
                w->weaponData.type == RE::WEAPON_TYPE::kMine)
                continue;
            for (auto *stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (!stack->flags.any(RE::BGSInventoryItem::Stack::Flags::kSlotMask))
                    continue;
                const int type = static_cast<int>(w->weaponData.type.underlying());
                if (!LockMath::Melee(type))
                    return false; // ranged always wins ambiguity
                melee = true;
            }
        }
        return melee;
    }
    inline bool ControlsReserved() {
        auto *p = RE::PlayerCharacter::GetSingleton();
        return ready && g_config.enabled && g_config.targetLock && Gameplay() && p &&
               p->lifeState == RE::ACTOR_LIFE_STATE::kAlive &&
               (cameraScope || movementScope || p->weaponState == RE::WEAPON_STATE::kDrawn) &&
               EquippedMelee(p);
    }
    inline bool Context() {
        auto *c = RE::PlayerCamera::GetSingleton();
        if (!ControlsReserved() || !c || !c->IsStateActive(RE::CameraStates::kThirdPerson))
            return false;
        auto state = c->GetState();
        return state && !static_cast<RE::ThirdPersonState *>(state.get())->ironSights &&
               Now() >= EquipGuard::until;
    }
    inline void Reset() {
        Unlock(false);
        orbitHeld = false;
    }
    inline void ReleaseInvalidContext() {
        // Holstering invalidates Context before the next target tick. Give the
        // camera its release callback before clearing ownership of the lock.
        Unlock();
        orbitHeld = false;
    }
    inline bool ProtectCamera() {
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
        // Rotate the camera's local forward (0,1,0) by its world quaternion.
        const auto f = CombatMath::Forward(q.w, q.x, q.y, q.z);
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
        // Prefer the race's central torso bone, not a head/weapon/limb lock marker.
        if (a->race && a->race->bodyPartData && a->Get3D()) {
            auto *part = a->race->bodyPartData->partArray[0];
            if (part && !part->targetName.empty()) {
                if (auto *node =
                        RE::BSUtilities::GetObjectByName(a->Get3D(), part->targetName, true, false))
                    return node->world.translation;
            }
        }
        // Creature-aware fallback rather than a fixed human-height offset.
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
               std::hypot(d.x, d.y, d.z) < g_config.lockDistance;
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
                orbitHeld = true;
                lockRequested = true; // one continuous camera session
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
        auto *p = RE::PlayerCharacter::GetSingleton();
        auto a = target.get();
        if (a && Valid(p, a.get()))
            return true;
        // Never read a dying/ragdoll torso as a new camera anchor. Handoff without
        // passing through the unlocked/native-facing mode, even if the handle died.
        if (Select(0, true))
            return true;
        Unlock(); // orbitHeld survives: free mouse look resumes from the same orbit
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
        const auto d = Point(a.get()) - p->GetPosition();
        bodyYaw = CombatMath::Turn(p->data.angle.z, std::atan2(d.x, d.y), g_config.lockResponse,
                                   frameDelta);
        p->data.angle.z = bodyYaw;
        // Let the native update propagate yaw without resynchronizing position.
    }
    inline void RestoreFacing(RE::PlayerCharacter *p) {
        if (p && Active()) {
            p->data.angle.z = bodyYaw;
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
        // Feedback from actual camera orientation includes the shoulder offset.
        // Change the existing orbit, not actor pitch, and never reset on unlock.
        const auto correction = LockMath::CameraCorrection(d.x, d.y, d.z, f.x, f.y, f.z,
                                                           g_config.lockResponse, frameDelta);
        state->freeRotation.x = Logic::SignedYaw(state->freeRotation.x + correction[0]);
        state->freeRotation.y = std::clamp(state->freeRotation.y + correction[1], -1.2F, 1.2F);
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
    inline bool LockButton(const RE::InputEvent *e) {
        const auto *button = e->As<RE::ButtonEvent>();
        return button && ControllerInput::LockButton(
            e->device == RE::INPUT_DEVICE::kGamepad, button->QIDCode());
    }
    inline const RE::ThumbstickEvent *LookStick(const RE::InputEvent *e) {
        const auto *stick = e->As<RE::ThumbstickEvent>();
        return stick && ControllerInput::LookStick(
            e->device == RE::INPUT_DEVICE::kGamepad, stick->QIDCode()) ? stick : nullptr;
    }
    // Pure filtering also used by the camera receiver: it must NOT toggle twice.
    inline bool Consume(const RE::InputEvent *e) {
        if (!ControlsReserved())
            return false;
        const int code = MouseButton(e);
        if (ControllerContext() && LockButton(e))
            return true;
        if (Active() && LookStick(e))
            return true;
        if (code == 2)
            return true; // Keep middle-click reserved for melee target lock.
        // Unlocked wheel input reaches vanilla zoom/POV handling in both receivers.
        return Active() && (code == 8 || code == 9 || e->As<RE::MouseMoveEvent>());
    }
    inline bool Input(const RE::InputEvent *e) {
        if (!ControlsReserved()) {
            Unlock();
            return false;
        }
        const bool consume = Consume(e);
        if (!Context()) {
            Unlock();
            return consume;
        }
        const int code = MouseButton(e);
        if (auto *button = e->As<RE::ButtonEvent>();
            button && e->device == RE::INPUT_DEVICE::kMouse && button->QPressed()) {
            static unsigned mouseReports = 0;
            if (mouseReports++ < 12)
                REX::LogInformation("Melee mouse input: raw={:X}, event={}, reserved={}",
                                    button->QIDCode(), button->QUserEvent().c_str(), consume);
        }
        if (auto *b = e->As<RE::ButtonEvent>()) {
            if ((code == 2 || (ControllerContext() && LockButton(e))) && b->QJustPressed()) {
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
                if (std::abs(mouseTravel) >= g_config.switchPixels) {
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
            LookDevice::Observe(head);
            EquipGuard::FilteredQueue queue(head, Consume);
            original(self, queue.head);
        }
        static bool Install() {
            if (original)
                return true;
            auto *c = RE::PlayerCamera::GetSingleton();
            if (!c)
                return false;
            auto *receiver = static_cast<RE::BSInputEventReceiver *>(c);
            HookBatch hooks(*reinterpret_cast<std::uintptr_t **>(receiver));
            hooks.Add(0, Thunk, original);
            if (hooks.Commit())
                return true;
            original = nullptr;
            return false;
        }
    };
} // namespace TrueThirdPerson::MeleeLock
