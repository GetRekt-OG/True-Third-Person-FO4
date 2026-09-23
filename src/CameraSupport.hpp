#pragma once
#include "Logic.hpp"
#include "LookSensitivity.hpp"
#include "RE/P/PowerArmor.hpp"
// Shared support for the supplied Independent Weapon Facing camera behavior.
namespace TrueThirdPerson {
    struct Config {
        LookSensitivity::Profile controllerSensitivity{{1.0F, 1.0F}, {1.0F, 0.5F}, {1.0F, 1.0F}};
        LookSensitivity::Profile mouseSensitivity;
        unsigned adsTurnMs = 250;
        unsigned hipFireHoldMs = 3000;
        bool lockMarker = true;
        bool cameraCompass = true;
        bool hideSwimmingWeapon = true;
        bool targetLock = true;
        float lockDistance = 2000, lockResponse = 12, switchPixels = 100;
        bool enabled = true;
        bool hipFireFacing = true;
        bool smoothHipFire = true;
        bool keepHolsterView = true;
    };
    inline Config g_config;
    inline bool g_gameReady = false;
    inline std::filesystem::path g_ini;
    inline bool InPowerArmor(const RE::PlayerCharacter *player) {
        return player && RE::PowerArmor::ActorInPowerArmor(*player);
    }
    inline bool WeaponDrawn(const RE::PlayerCharacter *p) {
        return p &&
               std::to_underlying(p->weaponState) >= std::to_underlying(RE::WEAPON_STATE::kDrawn);
    }
    inline bool Gameplay() {
        if (!g_gameReady)
            return false;
        DWORD process = 0;
        const auto foreground = GetForegroundWindow();
        if (!foreground || !GetWindowThreadProcessId(foreground, &process) ||
            process != GetCurrentProcessId())
            return false;
        const auto *ui = RE::UI::GetSingleton();
        return ui && ui->menuMode == 0;
    }
    inline void LoadSensitivity(const wchar_t* section, LookSensitivity::Profile& profile) {
        const auto read = [&](const wchar_t* key, float fallback) {
            wchar_t value[64]{};
            GetPrivateProfileStringW(section, key, L"", value, 64, g_ini.c_str());
            return LookSensitivity::Parse(value, fallback);
        };
        profile.holstered.x = read(L"X-Axis Sensitivity", profile.holstered.x);
        profile.holstered.y = read(L"Y-Axis Sensitivity", profile.holstered.y);
        profile.drawn.x = read(L"Drawn X-Axis Sensitivity", profile.drawn.x);
        profile.drawn.y = read(L"Drawn Y-Axis Sensitivity", profile.drawn.y);
        profile.ads.x = read(L"ADS X-Axis Sensitivity", profile.ads.x);
        profile.ads.y = read(L"ADS Y-Axis Sensitivity", profile.ads.y);
    }
    inline void LoadConfig() {
        wchar_t exe[32768]{};
        const DWORD count = GetModuleFileNameW(nullptr, exe, 32768);
        const auto root = count > 0 && count < 32768 ? std::filesystem::path(exe).parent_path()
                                                     : std::filesystem::current_path();
        g_ini = root / L"Data/F4SE/Plugins/TrueThirdPerson.ini";
        g_config = Config{};
        LoadSensitivity(L"Controller", g_config.controllerSensitivity);
        LoadSensitivity(L"MouseKeyboard", g_config.mouseSensitivity);
        g_config.enabled = GetPrivateProfileIntW(L"Main", L"Enabled", 1, g_ini.c_str()) != 0;
        g_config.hipFireHoldMs = std::clamp(
            GetPrivateProfileIntW(L"Main", L"HipFireHoldMs", 3000, g_ini.c_str()), 275U, 5000U);
        g_config.targetLock =
            GetPrivateProfileIntW(L"TargetLock", L"Enabled", 1, g_ini.c_str()) != 0;
        g_config.lockDistance = static_cast<float>(std::clamp(
            GetPrivateProfileIntW(L"TargetLock", L"Distance", 2000, g_ini.c_str()), 200U, 10000U));
        g_config.lockResponse = static_cast<float>(std::clamp(
            GetPrivateProfileIntW(L"TargetLock", L"Response", 12, g_ini.c_str()), 1U, 40U));
        g_config.switchPixels = static_cast<float>(std::clamp(
            GetPrivateProfileIntW(L"TargetLock", L"SwitchMousePixels", 100, g_ini.c_str()), 20U,
            1000U));

    }
    // All entries in one table are installed as a transaction; rollback on failure.
    class HookBatch {
        struct Entry {
            std::size_t slot;
            std::uintptr_t oldValue;
            std::uintptr_t newValue;
        };
        std::uintptr_t *table;
        std::array<Entry, 6> entries{};
        std::size_t size = 0;

      public:
        explicit HookBatch(std::uintptr_t *value) : table(value) {}
        template <class F> void Add(std::size_t slot, F thunk, F &original) {
            if (!table || size == entries.size())
                REX::Fail("Invalid camera hook table");
            original = std::bit_cast<F>(table[slot]);
            entries[size++] = {slot, table[slot], std::bit_cast<std::uintptr_t>(thunk)};
        }
        bool Commit() {
            for (std::size_t i = 0; i < size; ++i) {
                if (!entries[i].oldValue)
                    return false;
            }
            for (std::size_t i = 0; i < size; ++i) {
                const auto &e = entries[i];
                const auto error = REL::WriteSafeData(table + e.slot, e.newValue);
                if (error.value() == REX::ERROR_NUMBER_SUCCESS)
                    continue;
                while (i > 0) {
                    const auto &previous = entries[--i];
                    if (REL::WriteSafeData(table + previous.slot, previous.oldValue).value() !=
                        REX::ERROR_NUMBER_SUCCESS)
                        REX::Fail("Cannot restore camera hook table");
                }
                REX::LogError("Hook installation failed: {}", error.value());
                return false;
            }
            return true;
        }
    };
} // namespace TrueThirdPerson
