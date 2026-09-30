#pragma once
#include "CameraSupport.hpp"
#include "RE/B/BSScript_IVirtualMachine.hpp"
#include "RE/B/BSScript_Variable.hpp"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

// "Reset to defaults" on the MCM main page. MCM buttons can only run Papyrus
// scripts and TTP ships none, so the control is an ordinary switch that TTP
// watches while the pause menu is open. Each value from the MCM defaults file
// goes back through MCM's own Papyrus setters (so the open menu can show it)
// and into the settings file (so TTP reads it even if those calls fail).
// bUseMCM is left alone so the reset values are the ones in use.
namespace TrueThirdPerson::McmReset {
    inline RE::BSScript::IVirtualMachine *vm = nullptr;
    inline std::filesystem::path defaults, settings;
    inline std::atomic_bool pauseMenuOpen{false};
    inline std::atomic_bool queued{false};
    inline std::atomic<double> refreshAt{0};

    inline bool F4SE_API BindVM(RE::BSScript::IVirtualMachine *machine) {
        vm = machine;
        return true;
    }

    inline bool Requested() {
        return GetPrivateProfileIntW(L"Main", L"bResetDefaults", 0, settings.c_str()) != 0;
    }

    // Queues MCM.<function>(modName, id[, value]). Papyrus runs it later.
    template <class... Value> bool CallMCM(const char *function, std::string id, Value... value) {
        if (!vm)
            return false;
        const RE::BSFixedString script("MCM"), name(function);
        const RE::BSTThreadScrapFunction<bool(RE::BSScrapArray<RE::BSScript::Variable> &)> args =
            [id = std::move(id), value...](RE::BSScrapArray<RE::BSScript::Variable> &out) {
                out.clear();
                if (!id.empty()) {
                    out.emplace_back(RE::BSFixedString("TrueThirdPerson"));
                    out.emplace_back(RE::BSFixedString(id.c_str()));
                }
                (out.emplace_back(value), ...);
                return true;
            };
        return vm->DispatchStaticCall(script, name, args, nullptr);
    }

    inline std::string Narrow(std::wstring_view text) {
        std::string out;
        for (const wchar_t c : text)
            out += c < 0x80 ? static_cast<char>(c) : '?';
        return out;
    }

    // Splits the double-null-terminated lists the profile API returns.
    inline std::vector<std::wstring> Entries(const std::vector<wchar_t> &buffer) {
        std::vector<std::wstring> out;
        for (const wchar_t *p = buffer.data(); *p; p += std::wcslen(p) + 1)
            out.emplace_back(p);
        return out;
    }

    // Main thread. Returns true when a requested reset was carried out.
    inline bool Run() {
        if (!Requested())
            return false;
        std::vector<wchar_t> buffer(32768);
        GetPrivateProfileSectionNamesW(buffer.data(), static_cast<DWORD>(buffer.size()),
                                       defaults.c_str());
        int written = 0, sent = 0;
        for (const auto &section : Entries(buffer)) {
            std::vector<wchar_t> pairs(32768);
            GetPrivateProfileSectionW(section.c_str(), pairs.data(), static_cast<DWORD>(pairs.size()),
                                      defaults.c_str());
            for (const auto &line : Entries(pairs)) {
                const auto split = line.find(L'=');
                if (line.empty() || line[0] == L';' || split == std::wstring::npos)
                    continue;
                const auto key = line.substr(0, split), value = line.substr(split + 1);
                if (key == L"bUseMCM" || key == L"bResetDefaults")
                    continue;
                written += WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(),
                                                      settings.c_str()) != 0;
                const auto id = Narrow(key) + ':' + Narrow(section);
                bool ok = false;
                switch (key[0]) {
                case L'b':
                    ok = CallMCM("SetModSettingBool", id, std::wcstol(value.c_str(), nullptr, 10) != 0);
                    break;
                case L'i':
                    ok = CallMCM("SetModSettingInt", id,
                                 static_cast<std::int32_t>(std::wcstol(value.c_str(), nullptr, 10)));
                    break;
                case L'f':
                    ok = CallMCM("SetModSettingFloat", id, std::wcstof(value.c_str(), nullptr));
                    break;
                default:
                    break;
                }
                sent += ok;
            }
        }
        WritePrivateProfileStringW(L"Main", L"bResetDefaults", L"0", settings.c_str());
        CallMCM("SetModSettingBool", "bResetDefaults:Main", false);
        if (pauseMenuOpen)
            refreshAt = Now() + 0.3; // after the setters have run
        char line[160]{};
        std::snprintf(line, sizeof(line),
                      "MCM reset to defaults: %d values written, %d sent to MCM%s", written, sent,
                      vm ? "" : " (no Papyrus VM)");
        TTPDiagnostic::Trace(line);
        return true;
    }

    inline void Refresh() {
        if (!CallMCM("RefreshMenu", std::string{}))
            TTPDiagnostic::Trace("MCM reset: RefreshMenu call failed");
    }

    // Polls the switch while the pause menu is open. The work itself runs as a
    // game task on the main thread. Closing the pause menu also checks it.
    inline void Watch(const std::filesystem::path &data) {
        defaults = data / L"MCM/Config/TrueThirdPerson/settings.ini";
        settings = data / L"MCM/Settings/TrueThirdPerson.ini";
        const auto *tasks = F4SE::GetTaskInterface().get();
        std::thread([tasks] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                const double at = refreshAt;
                if (at > 0 && Now() >= at) {
                    refreshAt = 0;
                    tasks->AddTask([] { Refresh(); });
                }
                if (pauseMenuOpen && !queued && Requested()) {
                    queued = true;
                    tasks->AddTask([] {
                        Run();
                        queued = false;
                    });
                }
            }
        }).detach();
    }
}
