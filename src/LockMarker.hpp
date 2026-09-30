#pragma once
#include "TargetLock.hpp"
#include "RE/G/GameMenuBase.hpp"
#include "RE/B/BSScaleformManager.hpp"
#include "RE/H/HUDMenuUtils.hpp"
#include "RE/N/NiColor.hpp"
#include "RE/U/UIMessageQueue.hpp"
#include "RE/U/UI_MESSAGE_TYPE.hpp"
#include "RE/U/UI_MENU_FLAGS.hpp"
#include "RE/U/UI_DEPTH_PRIORITY.hpp"
#include "Scaleform/G/GFx_Value.hpp"

namespace TrueThirdPerson::LockMarker {
    inline constexpr const char *menuName = "TrueThirdPersonLockMarker";
    // Screen position, worked out on the main thread (Track). The menu may advance
    // on a UI job thread, so it only reads these and never touches game objects.
    inline std::atomic<float> screenX{0}, screenY{0};
    inline std::atomic_bool onScreen{false};
    inline std::atomic_bool movieFailed{false}; // SWF missing: don't show the menu again
    inline bool Enabled() {
        return TargetLock::SettingsFor(TargetLock::lockedMode).marker;
    }
    class Menu final : public RE::GameMenuBase {
      public:
        Menu() {
            // kAllowSaving: the overlay must never block a save while a lock is active.
            menuFlags.set(RE::UI_MENU_FLAGS::kDoNotHideCursorWhenTopmost);
            menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
            menuFlags.set(RE::UI_MENU_FLAGS::kAllowSaving);
            if (!F4SE::IsRuntimeOnlyOG()) // OG loses game audio with this flag
                menuFlags.set(RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu);
            depthPriority = RE::UI_DEPTH_PRIORITY::kHUDMenu;
            auto *manager = RE::BSScaleformManager::GetSingleton();
            const bool loaded =
                manager &&
                manager->LoadMovie(*this, "TrueThirdPerson/LockMarker", "",
                                   ::Scaleform::GFx::Movie::ScaleModeType::kExactFit, 0.0F);
            if (!loaded)
                movieFailed = true;
            TTPDiagnostic::Trace(loaded ? "Lock marker SWF loaded" : "Lock marker SWF load failed");
        }
        void AdvanceMovie(float dt, std::uint64_t time) override {
            if (uiMovie) {
                const bool visible = onScreen;
                const float x = screenX, y = screenY;
                using Value = ::Scaleform::GFx::Value;
                const auto color = RE::HUDMenuUtils::GetGameplayHUDColor();
                Value args[7]{Value(static_cast<double>(x)),
                              Value(static_cast<double>(1.0F - y)),
                              Value(visible),
                              Value(1.0),
                              Value(static_cast<double>(color.r)),
                              Value(static_cast<double>(color.g)),
                              Value(static_cast<double>(color.b))};
                const bool sent = uiMovie->Invoke("root.updateMarker", nullptr, args, 7);
                if (!sent && !reportedFailure) {
                    reportedFailure = true;
                    TTPDiagnostic::Trace("Lock marker updateMarker call failed");
                }
            }
            RE::GameMenuBase::AdvanceMovie(dt, time);
        }

      private:
        bool reportedFailure = false;
    };
    inline RE::IMenu *Create(const RE::UIMessage &) {
        return new Menu();
    }
    inline void Hide() {
        auto *queue = RE::UIMessageQueue::GetSingleton();
        const RE::BSFixedString name(menuName);
        if (queue && MenuOpen(menuName) && !queue->HasMessage(name)) {
            queue->AddMessage(name, RE::UI_MESSAGE_TYPE::kHide);
            TTPDiagnostic::Trace("Lock marker hidden");
        }
    }

    // The overlay is only on the menu stack while a target is locked in normal
    // gameplay. It never shares the stack with a menu, so it can't interfere
    // with the menu cursor (workbenches, crafting, Pip-Boy, ...).
    inline bool Wanted() {
        const auto *ui = RE::UI::GetSingleton();
        return Enabled() && !movieFailed && g_gameReady && ui && ui->menuMode == 0 &&
               !Workbench::suspended && TargetLock::lockRequested;
    }

    // True for menus the overlay must not coexist with.
    inline bool ConflictsWith(const RE::BSFixedString &opening) {
        if (opening == menuName)
            return false;
        const auto *ui = RE::UI::GetSingleton();
        const auto menu = ui ? ui->GetMenu(opening) : nullptr;
        return !menu || menu->menuFlags.any(RE::UI_MENU_FLAGS::kUsesCursor,
                                            RE::UI_MENU_FLAGS::kUsesMenuContext,
                                            RE::UI_MENU_FLAGS::kPausesGame);
    }

    // Main thread, once per player update after the native update: where the
    // marker goes this frame.
    inline void Track() {
        bool visible = false;
        RE::NiPoint3 screen{0, 0, 0};
        if (Enabled() && TargetLock::lockRequested &&
            (TargetLock::Context() || TargetLock::AimPaused())) {
            auto target = TargetLock::target.get();
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (target && TargetLock::Valid(player, target.get())) {
                screen = RE::HUDMenuUtils::WorldPtToScreenPt3(TargetLock::Point(target.get()));
                visible = std::isfinite(screen.x) && std::isfinite(screen.y) &&
                          std::isfinite(screen.z) && screen.z >= 0 && screen.x >= 0 &&
                          screen.x <= 1 && screen.y >= 0 && screen.y <= 1;
            }
        }
        screenX = visible ? screen.x : 0.0F;
        screenY = visible ? screen.y : 0.0F;
        onScreen = visible;
    }

    // Called every player update: opens or closes the overlay to match Wanted().
    inline void Ensure() {
        if (!Wanted()) {
            Hide();
            return;
        }
        auto *ui = RE::UI::GetSingleton();
        auto *queue = RE::UIMessageQueue::GetSingleton();
        const RE::BSFixedString name(menuName);
        if (!queue || MenuOpen(menuName) || queue->HasMessage(name))
            return;
        if (!ui->IsMenuRegistered(name) && !ui->RegisterMenu(name, Create))
            return;
        queue->AddMessage(name, RE::UI_MESSAGE_TYPE::kShow);
        TTPDiagnostic::Trace("Lock marker shown");
    }
}
