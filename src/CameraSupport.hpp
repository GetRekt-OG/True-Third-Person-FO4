#pragma once
#include "Logic.hpp"
#include "StartupLog.hpp"
#include "RE/I/IMenu.hpp"
#include "RE/P/PowerArmor.hpp"
#include "RE/U/UI_MENU_FLAGS.hpp"

// Configuration and shared game-state helpers.
namespace TrueThirdPerson {
    struct LockSettings {
        bool enabled = true;
        bool marker = true;
        float distance = 2000, response = 12, switchPixels = 100;
        unsigned key = 0, gamepadKey = 0; // LockKeys::keyboardMouse / gamepad index
    };
    // Added to the game's shoulder offset: x right, y forward, z up.
    struct ShoulderOffset {
        float x = 0, y = 0, z = 0;
        bool operator==(const ShoulderOffset &) const = default;
    };
    struct Config {
        LookSensitivity::Profile controllerSensitivity{{1.0F, 1.0F}, {1.0F, 0.5F}, {1.0F, 1.0F}};
        LookSensitivity::Profile mouseSensitivity;
        unsigned adsTurnMs = 250;
        unsigned hipFireHoldMs = 1500;
        bool cameraCompass = true;
        bool hideSwimmingWeapon = true;
        LockSettings meleeLock;
        LockSettings rangedLock{.enabled = false, .distance = 3000};
        bool enabled = true;
        bool hipFireFacing = true;
        bool smoothHipFire = true;
        bool keepHolsterView = true;
        bool onlyWhenRelaxed = false; // a ready gun gets the game's own camera and movement
        unsigned twistDegrees = 60;   // hip-fire torso twist limit, 0 = turn the whole body
        float twistSign = 1;          // [Debug] TwistSign=-1 turns the spine the other way
        ShoulderOffset holsteredOffset, rangedOffset, meleeOffset;
        bool drawTrace = false; // [Debug] DrawTrace: log movement for each weapon draw
        bool customRelax = true; // replace the game's raised-weapon time
        float relaxSeconds = 0.7F; // ...with this (fGunPlayerRelaxedWaitTime)
    };
    inline Config g_config;
    // "Only when relaxed": set while a gun is out and ready. TTP then behaves as
    // if switched off until the gun relaxes again.
    inline bool g_readySuspended = false;
    // This frame's movement stick (camera-relative), before any remapping.
    inline RE::NiPoint2 g_moveStick{};
    inline bool TTPActive() {
        return g_config.enabled && !g_readySuspended;
    }
    inline bool g_gameReady = false;
    inline std::filesystem::path g_ini;
    inline std::atomic_bool g_cameraHooksInstalled{false};
    inline std::atomic_bool g_controlsHookInstalled{false};
    inline std::atomic_bool g_directionalHooksInstalled{false};

    inline double Now() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // Short-lived result cache for checks that run many times per frame (every
    // input event, every camera hook). A few milliseconds is well under a frame.
    template <class T> struct CachedValue {
        double at = -1;
        T value{};
        template <class F> T Get(double ttl, F &&compute) {
            const double now = Now();
            if (now < at || now - at > ttl) {
                value = compute();
                at = now;
            }
            return value;
        }
        void Invalidate() { at = -1; }
    };
    using Cached = CachedValue<bool>;
    inline constexpr double kFrameCache = 0.004;

    // Optional self-timing ([Debug] Profile=1). Measures time spent in TTP's own
    // hook code per frame, excluding the game functions the hooks call into, and
    // writes a summary to the startup log every 5 seconds.
    namespace Profile {
        inline bool enabled = false;
        inline double frame = 0; // TTP time in the current frame
        inline thread_local int depth = 0;
        inline thread_local double excluded = 0;
        inline double lastFrame = 0, sum = 0, worst = 0, gameSum = 0, nextReport = 0;
        inline unsigned frames = 0;

        // Outermost TTP hook on the stack measures; nested ones are already covered.
        struct Scope {
            double start = 0, excludedAtStart = 0;
            bool counted = false, on = false;
            Scope() {
                if (!enabled)
                    return;
                counted = true;
                if (depth++ == 0) {
                    on = true;
                    start = Now();
                    excludedAtStart = excluded;
                }
            }
            ~Scope() {
                if (on)
                    frame += (Now() - start) - (excluded - excludedAtStart);
                if (counted)
                    --depth;
            }
            Scope(const Scope &) = delete;
            Scope &operator=(const Scope &) = delete;
        };

        // Calls into the game: not TTP's time. TTP hooks reached from inside
        // it measure themselves. Exclusions made inside this call are already
        // part of its elapsed time, so they are replaced, not added to.
        template <class F, class... Args> decltype(auto) Call(F function, Args &&...args) {
            if (!enabled)
                return function(std::forward<Args>(args)...);
            struct Native {
                double start = Now();
                double excludedBefore = excluded;
                int savedDepth = std::exchange(depth, 0);
                ~Native() {
                    excluded = excludedBefore + (Now() - start);
                    depth = savedDepth;
                }
            } native;
            return function(std::forward<Args>(args)...);
        }

        // Once per player update.
        inline void EndFrame() {
            if (!enabled)
                return;
            const double now = Now();
            if (lastFrame > 0) {
                sum += frame;
                worst = std::max(worst, frame);
                gameSum += now - lastFrame;
                ++frames;
            }
            lastFrame = now;
            frame = 0;
            if (now < nextReport || frames == 0)
                return;
            char line[200]{};
            std::snprintf(line, sizeof(line),
                          "Profile: TTP %.1f us/frame avg, %.1f us worst; game frame %.2f ms avg "
                          "(%u frames)",
                          sum / frames * 1e6, worst * 1e6, gameSum / frames * 1e3, frames);
            TTPDiagnostic::Trace(line);
            sum = worst = gameSum = 0;
            frames = 0;
            nextReport = now + 5.0;
        }
    }

    inline bool MenuOpen(const char *name) {
        const auto *ui = RE::UI::GetSingleton();
        return ui && ui->IsMenuOpen(RE::BSFixedString(name)).value_or(false);
    }
    inline bool VATSMenuOpen() {
        static Cached cache;
        return cache.Get(kFrameCache, [] { return MenuOpen("VATSMenu"); });
    }
    inline bool GameFocused() {
        static Cached cache;
        return cache.Get(kFrameCache, [] {
            DWORD process = 0;
            const auto foreground = GetForegroundWindow();
            return foreground && GetWindowThreadProcessId(foreground, &process) &&
                   process == GetCurrentProcessId();
        });
    }

    namespace Workbench {
        inline bool suspended = false;
        inline bool resumePending = false;
        // Crafting can rebuild the bench menu fast enough that it reads as closed
        // for a frame or two. Resume only after it has stayed closed this long.
        inline double closedSince = 0;
        inline constexpr double kCloseDebounceSeconds = 1.35;
        // Menus that take over mouse/camera input.
        inline constexpr const char *menus[]{"CookingMenu",        "CraftingMenu",
                                             "ExamineMenu",        "ExamineConfirmMenu",
                                             "PowerArmorModMenu", "RobotModMenu",
                                             "WorkshopMenu"};
        inline bool Open() {
            static Cached cache;
            return cache.Get(kFrameCache, [] {
                for (const auto *name : menus)
                    if (MenuOpen(name))
                        return true;
                return false;
            });
        }
    }

    // Base-game menus that take menu context. Any other menu that does is a mod
    // overlay (a favorites wheel, for example) that leaves the game and the
    // third-person camera running, so TTP carries on underneath it. The vanilla
    // favorites menu counts as an overlay too; wheel mods replace it.
    inline bool BaseGameMenu(std::string_view name) {
        static constexpr std::string_view names[]{
            "BarterMenu",        "BookMenu",           "Console",
            "ContainerMenu",     "CookingMenu",        "CraftingMenu",
            "CreditsMenu",       "CursorMenu",         "DialogueMenu",
            "ExamineConfirmMenu", "ExamineMenu",       "FaderMenu",
            "GenericMenu",       "HUDMenu",            "LevelUpMenu",
            "LoadingMenu",       "LockpickingMenu",    "LooksMenu",
            "MainMenu",          "MessageBoxMenu",     "MultiActivateMenu",
            "PauseMenu",         "PipboyHolotapeMenu", "PipboyMenu",
            "PowerArmorHUDMenu", "PowerArmorModMenu",  "PromptMenu",
            "QuantityMenu",      "RobotModMenu",       "ScopeMenu",
            "SitWaitMenu",       "SleepWaitMenu",      "SPECIALMenu",
            "TerminalHolotapeMenu", "TerminalMenu",    "TerminalMenuButtons",
            "VATSMenu",          "VignetteMenu",       "Workshop_CaravanMenu",
            "WorkshopMenu"};
        return std::find(std::begin(names), std::end(names), name) != std::end(names);
    }
    inline bool OverlayMenusOnly() {
        static Cached cache;
        return cache.Get(kFrameCache, [] {
            const auto *ui = RE::UI::GetSingleton();
            if (!ui)
                return false;
            bool overlay = false;
            for (const auto &menu : ui->menuStack) {
                if (!menu || !menu->menuFlags.all(RE::UI_MENU_FLAGS::kUsesMenuContext))
                    continue;
                if (BaseGameMenu(menu->menuName.c_str()))
                    return false;
                overlay = true;
            }
            return overlay;
        });
    }

    inline bool Gameplay() {
        if (!g_gameReady || Workbench::suspended || !GameFocused())
            return false;
        const auto *ui = RE::UI::GetSingleton();
        return ui && (ui->menuMode == 0 || OverlayMenusOnly());
    }

    inline bool InPowerArmor(const RE::PlayerCharacter *player) {
        return player && RE::PowerArmor::ActorInPowerArmor(*player);
    }
    inline bool WeaponDrawn(const RE::PlayerCharacter *p) {
        return p &&
               std::to_underlying(p->weaponState) >= std::to_underlying(RE::WEAPON_STATE::kDrawn);
    }
    // A gun is equipped and its model is attached.
    inline bool GunModelReady(const RE::PlayerCharacter *player) {
        if (!player || !player->biped)
            return false;
        const auto *gun = player->biped->GetBipObject(RE::BIPED_OBJECT::kWeaponGun);
        return gun && gun->parent.object && gun->partClone;
    }

    // Camera state access.
    inline RE::ThirdPersonState *ThirdPersonState() {
        auto *camera = RE::PlayerCamera::GetSingleton();
        return camera ? static_cast<RE::ThirdPersonState *>(
                            camera->GetState(RE::CameraStates::kThirdPerson).get())
                      : nullptr;
    }
    inline bool IsCurrentCamera(const RE::TESCameraState *state) {
        const auto *camera = RE::PlayerCamera::GetSingleton();
        return camera && state && camera->GetState().get() == state;
    }
    inline bool IronSights(const RE::ThirdPersonState *state) {
        return state && state->ironSights;
    }
    // The third-person state, only while it is the active camera.
    inline RE::ThirdPersonState *ActiveThirdPerson() {
        auto *state = ThirdPersonState();
        return IsCurrentCamera(state) ? state : nullptr;
    }
    inline bool IronSightsActive() {
        return IronSights(ActiveThirdPerson());
    }

    // Settings
    inline constexpr unsigned kLastKeyboardKey = std::size(LockKeys::keyboardMouse) - 1;
    inline constexpr unsigned kLastGamepadKey = std::size(LockKeys::gamepad) - 1;
    inline void LoadSensitivity(const wchar_t *section, LookSensitivity::Profile &profile) {
        const auto read = [&](const wchar_t *key, float fallback) {
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

    // MCM keeps defaults and user choices in separate files. User choices win.
    inline void ApplyMCMSettings(const std::filesystem::path &data) {
        const auto defaults = data / L"MCM/Config/TrueThirdPerson/settings.ini";
        const auto settings = data / L"MCM/Settings/TrueThirdPerson.ini";
        if (!GetPrivateProfileIntW(L"Main", L"bUseMCM", 0, settings.c_str()))
            return;
        const auto integer = [&](const wchar_t *section, const wchar_t *key, unsigned fallback,
                                 unsigned minimum, unsigned maximum) {
            const auto base = GetPrivateProfileIntW(section, key, fallback, defaults.c_str());
            return std::clamp(GetPrivateProfileIntW(section, key, base, settings.c_str()), minimum,
                              maximum);
        };
        const auto number = [&](const wchar_t *section, const wchar_t *key, float fallback,
                                float minimum, float maximum) {
            wchar_t base[64]{}, value[64]{};
            GetPrivateProfileStringW(section, key, L"", base, 64, defaults.c_str());
            GetPrivateProfileStringW(section, key, base, value, 64, settings.c_str());
            return Logic::ParseNumber(value, fallback, minimum, maximum);
        };
        const auto sensitivity = [&](const wchar_t *section, const wchar_t *key, float fallback) {
            return number(section, key, fallback, 0.05F, 5.0F);
        };
        g_config.enabled = integer(L"Main", L"bEnabled", 1, 0, 1) != 0;
        g_config.onlyWhenRelaxed = integer(L"Main", L"bOnlyWhenRelaxed", 0, 0, 1) != 0;
        g_config.twistDegrees = integer(L"Main", L"iTorsoTwistDegrees", g_config.twistDegrees, 0, 80);
        g_config.hipFireHoldMs = integer(L"Main", L"iHipFireHoldMs", 1500, 275, 5000);
        g_config.customRelax = integer(L"Main", L"bCustomRelax", 1, 0, 1) != 0;
        g_config.relaxSeconds = number(L"Main", L"fRelaxSeconds", g_config.relaxSeconds, 0, 10);
        const auto lock = [&](const wchar_t *section, LockSettings &value) {
            const auto number = [&](const wchar_t *key, float current, unsigned low, unsigned high) {
                return static_cast<float>(integer(section, key, static_cast<unsigned>(current), low, high));
            };
            value.enabled = integer(section, L"bEnabled", value.enabled, 0, 1) != 0;
            value.marker = integer(section, L"bShowMarker", value.marker, 0, 1) != 0;
            value.distance = number(L"iDistance", value.distance, 200, 20000);
            value.response = number(L"iResponse", value.response, 1, 40);
            value.switchPixels = number(L"iSwitchMousePixels", value.switchPixels, 20, 1000);
            value.key = integer(section, L"iKey", value.key, 0, kLastKeyboardKey);
            value.gamepadKey = integer(section, L"iGamepadKey", value.gamepadKey, 0, kLastGamepadKey);
        };
        lock(L"TargetLock", g_config.meleeLock);
        lock(L"RangedTargetLock", g_config.rangedLock);
        const auto profile = [&](const wchar_t *section, LookSensitivity::Profile &value) {
            value.holstered.x = sensitivity(section, L"fHolsteredX", value.holstered.x);
            value.holstered.y = sensitivity(section, L"fHolsteredY", value.holstered.y);
            value.drawn.x = sensitivity(section, L"fDrawnX", value.drawn.x);
            value.drawn.y = sensitivity(section, L"fDrawnY", value.drawn.y);
            value.ads.x = sensitivity(section, L"fADSX", value.ads.x);
            value.ads.y = sensitivity(section, L"fADSY", value.ads.y);
        };
        profile(L"Controller", g_config.controllerSensitivity);
        profile(L"MouseKeyboard", g_config.mouseSensitivity);
        const auto offset = [&](const wchar_t *x, const wchar_t *y, const wchar_t *z,
                                ShoulderOffset &value) {
            value.x = number(L"CameraOffset", x, value.x, -200, 200);
            if (y)
                value.y = number(L"CameraOffset", y, value.y, -200, 200);
            value.z = number(L"CameraOffset", z, value.z, -200, 200);
        };
        offset(L"fHolsteredX", nullptr, L"fHolsteredZ", g_config.holsteredOffset);
        offset(L"fRangedX", L"fRangedY", L"fRangedZ", g_config.rangedOffset);
        offset(L"fMeleeX", L"fMeleeY", L"fMeleeZ", g_config.meleeOffset);
    }

    inline std::filesystem::path GameRoot() {
        wchar_t exe[32768]{};
        const DWORD count = GetModuleFileNameW(nullptr, exe, 32768);
        return count > 0 && count < 32768 ? std::filesystem::path(exe).parent_path()
                                          : std::filesystem::current_path();
    }

    inline void LoadConfig() {
        const auto root = GameRoot();
        g_ini = root / L"Data/F4SE/Plugins/TrueThirdPerson.ini";
        const auto integer = [](const wchar_t *section, const wchar_t *key, unsigned fallback,
                                unsigned minimum, unsigned maximum) {
            return std::clamp(GetPrivateProfileIntW(section, key, fallback, g_ini.c_str()),
                              minimum, maximum);
        };
        g_config = Config{};
        LoadSensitivity(L"Controller", g_config.controllerSensitivity);
        LoadSensitivity(L"MouseKeyboard", g_config.mouseSensitivity);
        g_config.enabled = GetPrivateProfileIntW(L"Main", L"Enabled", 1, g_ini.c_str()) != 0;
        g_config.onlyWhenRelaxed = integer(L"Main", L"OnlyWhenRelaxed", 0, 0, 1) != 0;
        g_config.twistDegrees = integer(L"Main", L"TorsoTwistDegrees", 60, 0, 80);
        g_config.hipFireHoldMs = integer(L"Main", L"HipFireHoldMs", 1500, 275, 5000);
        g_config.customRelax = integer(L"Main", L"CustomRelaxTime", 1, 0, 1) != 0;
        {
            wchar_t value[64]{};
            GetPrivateProfileStringW(L"Main", L"RelaxTime", L"", value, 64, g_ini.c_str());
            g_config.relaxSeconds = Logic::ParseNumber(value, g_config.relaxSeconds, 0, 10);
        }
        const auto lock = [&](const wchar_t *section, LockSettings &value) {
            const auto number = [&](const wchar_t *key, float current, unsigned low, unsigned high) {
                return static_cast<float>(integer(section, key, static_cast<unsigned>(current), low, high));
            };
            value.enabled = integer(section, L"Enabled", value.enabled, 0, 1) != 0;
            value.marker = integer(section, L"ShowMarker", value.marker, 0, 1) != 0;
            value.distance = number(L"Distance", value.distance, 200, 20000);
            value.response = number(L"Response", value.response, 1, 40);
            value.switchPixels = number(L"SwitchMousePixels", value.switchPixels, 20, 1000);
            value.key = integer(section, L"Key", value.key, 0, kLastKeyboardKey);
            value.gamepadKey = integer(section, L"GamepadKey", value.gamepadKey, 0, kLastGamepadKey);
        };
        lock(L"TargetLock", g_config.meleeLock);
        lock(L"RangedTargetLock", g_config.rangedLock);
        const auto offset = [](const wchar_t *key, float &value) {
            wchar_t text[64]{};
            GetPrivateProfileStringW(L"CameraOffset", key, L"", text, 64, g_ini.c_str());
            value = Logic::ParseNumber(text, value, -200, 200);
        };
        offset(L"HolsteredX", g_config.holsteredOffset.x);
        offset(L"HolsteredZ", g_config.holsteredOffset.z);
        offset(L"RangedX", g_config.rangedOffset.x);
        offset(L"RangedY", g_config.rangedOffset.y);
        offset(L"RangedZ", g_config.rangedOffset.z);
        offset(L"MeleeX", g_config.meleeOffset.x);
        offset(L"MeleeY", g_config.meleeOffset.y);
        offset(L"MeleeZ", g_config.meleeOffset.z);
        ApplyMCMSettings(root / L"Data");
        Profile::enabled = GetPrivateProfileIntW(L"Debug", L"Profile", 0, g_ini.c_str()) != 0;
        g_config.drawTrace = GetPrivateProfileIntW(L"Debug", L"DrawTrace", 0, g_ini.c_str()) != 0;
        g_config.twistSign =
            static_cast<int>(GetPrivateProfileIntW(L"Debug", L"TwistSign", 1, g_ini.c_str())) < 0
                ? -1.0F
                : 1.0F;
    }

    // Writes a set of vtable hooks as one transaction; rolls back on failure.
    class HookBatch {
        struct Entry {
            std::size_t slot;
            std::uintptr_t oldValue;
            std::uintptr_t newValue;
        };
        std::uintptr_t *table;
        std::array<Entry, 7> entries{};
        std::size_t size = 0;

      public:
        explicit HookBatch(std::uintptr_t *value) : table(value) {}
        template <class Object> static HookBatch For(Object *object) {
            return HookBatch(*reinterpret_cast<std::uintptr_t **>(object));
        }
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

    // Installs a single vtable hook on `object` and clears `original` on failure.
    template <class Object, class F> bool HookSingle(Object *object, std::size_t slot, F thunk,
                                                     F &original) {
        if (!object)
            return false;
        auto hooks = HookBatch::For(object);
        hooks.Add(slot, thunk, original);
        if (hooks.Commit())
            return true;
        original = nullptr;
        return false;
    }
}
