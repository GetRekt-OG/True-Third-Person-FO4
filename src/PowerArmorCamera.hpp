#pragma once
#include "REL/Trampoline.hpp"
#include "NativeHookLayout.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace TrueThirdPerson {
    // Select the camera's holstered branch without changing ActorState. Power
    // armor's Pip-Boy/weapon processing must always see the real weapon state.
    struct PowerArmorCamera {
        static inline thread_local bool scoped = false;
        static inline bool installed = false;
        static inline bool attempted = false;
        static inline bool (*original)(RE::Actor*) = nullptr;
        static inline REL::Trampoline trampoline{"TrueThirdPerson power armor camera"};

        static bool Predicate(RE::Actor* actor) {
            if (scoped && actor == RE::PlayerCharacter::GetSingleton())
                return true;
            return original(actor);
        }

        static bool Pivot(RE::Actor* actor) {
            if (scoped && actor == RE::PlayerCharacter::GetSingleton())
                return false;
            return actor->ShouldPivotToFaceCamera();
        }

        static bool Install() {
            if (attempted)
                return installed;
            attempted = true;
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto& db = REL::Iddb::GetSingleton();
            const auto predicate = base + db->GetOffset(2230295);
            const bool og = F4SE::IsRuntimeOnlyOG();
            const auto sites = NativeHookLayout::PowerCalls(og);
            std::array<std::uintptr_t, sites.size()> addresses{};
            for (std::size_t i = 0; i < sites.size(); ++i) {
                addresses[i] = base + db->GetOffset(sites[i].function) + sites[i].offset;
                const auto* code = reinterpret_cast<const std::uint8_t*>(addresses[i]);
                std::int32_t displacement = 0;
                std::memcpy(&displacement, code + 1, sizeof(displacement));
                if (code[0] != 0xE8 || addresses[i] + 5 + displacement != predicate ||
                    std::memcmp(code + 5, sites[i].following.data(), 2) != 0) {
                    TTPDiagnostic::Trace("Power armor camera: call validation failed; no patches applied");
                    return false;
                }
            }
            // Armed pivot checks can zero the orbit or rotate the camera with
            // the actor even when the holstered predicate is overridden.
            const auto pivotSites = NativeHookLayout::PowerPivots(og);
            constexpr std::array<std::uint8_t, 8> pivotBytes{
                0xFF, 0x90, 0xA8, 0x08, 0x00, 0x00, 0x84, 0xC0};
            std::array<std::uintptr_t, pivotSites.size()> pivotAddresses{};
            for (std::size_t i = 0; i < pivotSites.size(); ++i) {
                pivotAddresses[i] = base + db->GetOffset(pivotSites[i].function) +
                                    pivotSites[i].offset;
                if (std::memcmp(reinterpret_cast<const void*>(pivotAddresses[i]),
                                pivotBytes.data(), pivotBytes.size()) != 0) {
                    TTPDiagnostic::Trace("Power armor camera: pivot validation failed; no patches applied");
                    return false;
                }
            }
            original = reinterpret_cast<decltype(original)>(predicate);
            trampoline.Create(128);
            for (const auto address : addresses)
                trampoline.WriteCall<5>(address, Predicate);
            const auto pivotTarget =
                trampoline.AllocateBranch6(reinterpret_cast<std::uintptr_t>(&Pivot));
            for (const auto address : pivotAddresses) {
                const REL::Asm::Call6 call(address, pivotTarget);
                if (REL::WriteSafeData(address, call).value() != REX::ERROR_NUMBER_SUCCESS)
                    REX::Fail("Failed to install power armor camera pivot call");
            }
            installed = true;
            TTPDiagnostic::Trace("Power armor camera: scoped holstered branches installed, including rotation pivot");
            return true;
        }
    };
}
