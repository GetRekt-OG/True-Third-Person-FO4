#pragma once
#include "MeleeLock.hpp"
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
    class Menu final : public RE::GameMenuBase {
      public:
        Menu() {
            menuFlags.set(RE::UI_MENU_FLAGS::kAlwaysOpen);
            menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
            menuFlags.set(RE::UI_MENU_FLAGS::kAllowSaving);
            menuFlags.set(RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu);
            depthPriority = RE::UI_DEPTH_PRIORITY::kHUDMenu;
            auto *manager = RE::BSScaleformManager::GetSingleton();
            const bool loaded =
                manager &&
                manager->LoadMovie(*this, "TrueThirdPerson/LockMarker", "",
                                   ::Scaleform::GFx::Movie::ScaleModeType::kExactFit, 0.0F);
            TTPDiagnostic::Trace(loaded ? "Lock marker SWF loaded" : "Lock marker SWF load failed");
        }
        void AdvanceMovie(float dt, std::uint64_t time) override {
            if (uiMovie) {
                bool visible = false;
                RE::NiPoint3 screen{0, 0, 0};
                if (g_config.lockMarker && MeleeLock::Context() && MeleeLock::lockRequested) {
                    auto target = MeleeLock::target.get();
                    auto *player = RE::PlayerCharacter::GetSingleton();
                    if (target && MeleeLock::Valid(player, target.get())) {
                        screen =
                            RE::HUDMenuUtils::WorldPtToScreenPt3(MeleeLock::Point(target.get()));
                        visible = std::isfinite(screen.x) && std::isfinite(screen.y) &&
                                  std::isfinite(screen.z) && screen.z >= 0 && screen.x >= 0 &&
                                  screen.x <= 1 && screen.y >= 0 && screen.y <= 1;
                    }
                }
                using Value = ::Scaleform::GFx::Value;
                const auto color = RE::HUDMenuUtils::GetGameplayHUDColor();
                Value args[7]{Value(static_cast<double>(screen.x)),
                              Value(static_cast<double>(1.0F - screen.y)),
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
    inline void Ensure() {
        if (!g_gameReady || !g_config.lockMarker)
            return;
        static double nextCheck = 0;
        if (EquipGuard::Now() < nextCheck)
            return;
        nextCheck = EquipGuard::Now() + 1;
        auto *ui = RE::UI::GetSingleton();
        auto *queue = RE::UIMessageQueue::GetSingleton();
        if (!ui || !queue)
            return;
        const RE::BSFixedString name(menuName);
        if (!ui->IsMenuRegistered(name) && !ui->RegisterMenu(name, Create))
            return;
        if (!ui->IsMenuOpen(name).value_or(false) && !queue->HasMessage(name))
            queue->AddMessage(name, RE::UI_MESSAGE_TYPE::kShow);
    }
} // namespace TrueThirdPerson::LockMarker
