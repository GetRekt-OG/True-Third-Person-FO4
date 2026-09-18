#pragma once
#include "CameraSupport.hpp"
#include "RE/B/BipedAnim.hpp"
#include <vector>
#include "RE/B/BSVisit.hpp"

namespace TrueThirdPerson::WaterWeaponVisibility {
    struct HiddenRoot {
        RE::NiPointer<RE::NiAVObject> node;
        bool wasCulled = false;
    };
    inline std::vector<HiddenRoot> hidden;
    inline int reportedCount = -1;
    inline void Restore(HiddenRoot &entry) {
        if (entry.node) {
            entry.node->SetAppCulled(entry.wasCulled);
        }
    }
    inline void Reset() {
        for (auto &entry : hidden)
            Restore(entry);
        hidden.clear();
        if (reportedCount >= 0)
            REX::LogInformation("Water weapon visibility restored");
        reportedCount = -1;
    }
    inline void Update(RE::PlayerCharacter *player) {
        // Visual only. Never write weapon state, inventory, animation variables,
        // camera angles, movement input or physics state.
        const bool swimming =
            player && (player->swimming || player->DoGetCharacterState() ==
                                               RE::IMovementState::CHARACTER_STATE::kSwimming);
        if (!g_gameReady || !g_config.enabled || !g_config.hideSwimmingWeapon || !swimming ||
            player->lifeState != RE::ACTOR_LIFE_STATE::kAlive) {
            Reset();
            return;
        }
        std::vector<RE::NiAVObject *> current;
        auto collect = [&](RE::BipedAnim *biped) {
            if (!biped)
                return;
            for (int slot = static_cast<int>(RE::BIPED_OBJECT::kWeaponHand);
                 slot < static_cast<int>(RE::BIPED_OBJECT::kTotal); ++slot) {
                if (slot == static_cast<int>(RE::BIPED_OBJECT::kQuiver))
                    continue;
                auto *object = biped->GetBipObject(static_cast<RE::BIPED_OBJECT>(slot));
                auto *root = object ? object->partClone.get() : nullptr;
                if (root)
                    RE::BSVisit::TraverseScenegraphObjects(root, [&](RE::NiAVObject *node) {
                        if (std::find(current.begin(), current.end(), node) == current.end())
                            current.push_back(node);
                        return RE::BSVisit::BSVisitControl::kContinue;
                    });
            }
        };
        collect(player->biped.get());
        collect(player->firstPersonBipedAnim.get());
        if (reportedCount != static_cast<int>(current.size())) {
            reportedCount = static_cast<int>(current.size());
            REX::LogInformation("Swimming weapon visibility: {} scene objects found",
                                reportedCount);
        }
        // Release replaced weapon roots while retaining their original visibility.
        // NiPointer keeps a detached root alive until its restoration is complete.
        for (auto it = hidden.begin(); it != hidden.end();) {
            if (std::find(current.begin(), current.end(), it->node.get()) == current.end()) {
                Restore(*it);
                it = hidden.erase(it);
            } else
                ++it;
        }
        for (auto *root : current) {
            const auto found = std::find_if(hidden.begin(), hidden.end(),
                                            [&](const auto &e) { return e.node.get() == root; });
            if (found == hidden.end()) {
                HiddenRoot entry;
                entry.node.reset(root);
                entry.wasCulled = root->flags.any(RE::NiAVObject::Flags::kAppCulled);
                hidden.push_back(std::move(entry));
            }
        }
        // Snapshot the whole subtree before calling virtual setters, which may
        // propagate visibility. Reapply after animation/camera processing as well.
        for (auto *node : current)
            node->SetAppCulled(true);
    }
} // namespace TrueThirdPerson::WaterWeaponVisibility
