#pragma once
#include "CameraSupport.hpp"
#include "RE/B/BSVisit.hpp"
#include "RE/B/BipedAnim.hpp"
#include <vector>

// Hides weapon models while swimming. Purely visual: only the cull flag is
// touched, never weapon state, inventory, animation, camera or physics.
namespace TrueThirdPerson::WaterWeaponVisibility {
    struct HiddenRoot {
        RE::NiPointer<RE::NiAVObject> node;
        bool wasCulled = false;
    };
    // Never destroyed: releasing game nodes after the engine has shut down crashes.
    inline std::vector<HiddenRoot> &hidden = *new std::vector<HiddenRoot>;
    inline int reportedCount = -1;
    inline void Restore(HiddenRoot &entry) {
        if (entry.node)
            entry.node->SetAppCulled(entry.wasCulled);
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
        const bool swimming =
            player && (player->swimming || player->DoGetCharacterState() ==
                                               RE::IMovementState::CHARACTER_STATE::kSwimming);
        if (Workbench::suspended || !g_gameReady || !g_config.enabled ||
            !g_config.hideSwimmingWeapon || !swimming ||
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
        // Restore nodes that are no longer attached (weapon swapped). The NiPointer
        // keeps a detached node alive until then.
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
        // Record every node's original flag before any setter runs, since setters
        // can propagate down the tree. Called again after animation/camera updates.
        for (auto *node : current)
            node->SetAppCulled(true);
    }
}
