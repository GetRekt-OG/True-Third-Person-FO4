#pragma once
#include "CameraSupport.hpp"
#include "NativeHookLayout.hpp"
#include "REL/Trampoline.hpp"
#include <cstring>
#include <span>
#include <vector>

// Call-site patches inside engine functions. Every site is checked against the
// expected bytes first; if any check fails, nothing is written.
namespace TrueThirdPerson {
    namespace NativePatch {
        // Actor predicate the camera uses to pick its holstered/free-look branch.
        inline constexpr std::uint64_t kHolsteredCameraPredicate = 2230295;

        inline std::uintptr_t Resolve(std::uint64_t id, std::size_t offset = 0) {
            return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) +
                   REL::AddressResolver::GetSingleton()->GetOffset(id) + offset;
        }

        // Target of a 5-byte relative call.
        inline std::uintptr_t CallTarget(std::uintptr_t address) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, reinterpret_cast<const void *>(address + 1),
                        sizeof(displacement));
            return address + 5 + displacement;
        }

        // The original pivot calls are 6-byte vtable-indirect calls. Build the Call6
        // directly; WriteCall6 would try to decode a RIP-relative target that isn't there.
        inline void RedirectCall6(REL::Trampoline &trampoline, std::span<const std::uintptr_t> sites,
                                  std::uintptr_t function) {
            const auto target = trampoline.AllocateBranch6(function);
            for (const auto address : sites) {
                const REL::Asm::Call6 call(address, target);
                if (REL::WriteSafeData(address, call).value() != REX::ERROR_NUMBER_SUCCESS)
                    REX::Fail("Failed to install camera pivot call");
            }
        }
    }

    // In power armor the camera takes its holstered branch without touching the
    // actor's weapon state; the Pip-Boy and weapon code must still see the real one.
    struct PowerArmorCamera {
        static inline thread_local bool scoped = false;
        static inline bool installed = false;
        static inline bool attempted = false;
        static inline bool (*original)(RE::Actor *) = nullptr;
        // Never freed: game threads can still run through it while the DLL unloads.
        static inline REL::Trampoline &trampoline =
            *new REL::Trampoline("TrueThirdPerson power armor camera");

        static bool Active(RE::Actor *actor) {
            return !Workbench::suspended && scoped && actor == RE::PlayerCharacter::GetSingleton();
        }
        static bool Predicate(RE::Actor *actor) {
            return Active(actor) || original(actor);
        }
        static bool Pivot(RE::Actor *actor) {
            return !Active(actor) && actor->ShouldPivotToFaceCamera();
        }

        static bool Install() {
            if (attempted)
                return installed;
            attempted = true;
            const auto predicate = NativePatch::Resolve(NativePatch::kHolsteredCameraPredicate);
            const bool og = F4SE::IsRuntimeOnlyOG();

            const auto calls = NativeHookLayout::PowerCalls(og);
            std::array<std::uintptr_t, calls.size()> callSites{};
            for (std::size_t i = 0; i < calls.size(); ++i) {
                callSites[i] = NativePatch::Resolve(calls[i].function, calls[i].offset);
                const auto *code = reinterpret_cast<const std::uint8_t *>(callSites[i]);
                if (code[0] != 0xE8 || NativePatch::CallTarget(callSites[i]) != predicate ||
                    std::memcmp(code + 5, calls[i].following.data(), 2) != 0) {
                    TTPDiagnostic::Trace("Power armor camera: call check failed, not patched");
                    return false;
                }
            }
            // Armed pivot checks would otherwise zero the orbit or turn the camera
            // with the actor, even with the predicate overridden.
            const auto pivots = NativeHookLayout::PowerPivots(og);
            constexpr std::array<std::uint8_t, 8> pivotBytes{0xFF, 0x90, 0xA8, 0x08,
                                                             0x00, 0x00, 0x84, 0xC0};
            std::array<std::uintptr_t, pivots.size()> pivotSites{};
            for (std::size_t i = 0; i < pivots.size(); ++i) {
                pivotSites[i] = NativePatch::Resolve(pivots[i].function, pivots[i].offset);
                if (std::memcmp(reinterpret_cast<const void *>(pivotSites[i]), pivotBytes.data(),
                                pivotBytes.size()) != 0) {
                    TTPDiagnostic::Trace("Power armor camera: pivot check failed, not patched");
                    return false;
                }
            }

            original = reinterpret_cast<decltype(original)>(predicate);
            trampoline.Create(128);
            for (const auto address : callSites)
                trampoline.WriteCall<5>(address, Predicate);
            NativePatch::RedirectCall6(trampoline, pivotSites, reinterpret_cast<std::uintptr_t>(&Pivot));
            installed = true;
            return true;
        }
    };

    // The compass heading is orbit yaw + actor yaw unless the holstered predicate
    // is true. Our orbit already holds world yaw, so only the heading call sites
    // are redirected; the predicate itself is left alone everywhere else.
    struct CompassHeadingHook {
        static inline bool installed = false;
        static inline bool attempted = false;
        // One original per heading call: another mod may have hooked only one of them.
        static inline std::array<bool (*)(RE::Actor *), 2> originals{};
        static inline REL::Trampoline &trampoline = *new REL::Trampoline("TrueThirdPerson compass");

        // Deliberately independent of movement/menu eligibility.
        static bool UseCameraHeading(RE::Actor *actor) {
            auto *player = RE::PlayerCharacter::GetSingleton();
            auto *camera = RE::PlayerCamera::GetSingleton();
            return !Workbench::suspended && TTPActive() && g_config.cameraCompass &&
                   g_cameraHooksInstalled.load() && actor == player && player && camera &&
                   camera->IsStateActive(RE::CameraStates::kThirdPerson) &&
                   !IronSights(ThirdPersonState());
        }
        template <std::size_t I> static bool Heading(RE::Actor *actor) {
            return UseCameraHeading(actor) || originals[I](actor);
        }
        // During a pivot the heading code writes zero and skips the orbit math.
        // Report "no pivot" to the compass only; the actor's own flag is untouched.
        static bool Pivot(RE::Actor *actor) {
            return !UseCameraHeading(actor) && actor->ShouldPivotToFaceCamera();
        }

        static bool Install() {
            if (attempted)
                return installed;
            attempted = true;
            if (!g_config.cameraCompass)
                return false;
            const auto predicate = NativePatch::Resolve(NativePatch::kHolsteredCameraPredicate);
            const auto layout = NativeHookLayout::Compass(F4SE::IsRuntimeOnlyOG());
            std::vector<std::uintptr_t> callSites;
            std::vector<std::uintptr_t> pivotSites;
            for (const auto &site : layout) {
                callSites.push_back(NativePatch::Resolve(site.function, site.callOffset));
                pivotSites.push_back(NativePatch::Resolve(site.function, site.pivotOffset));
            }

            // Other mods (Full Body First Person, for one) hook these same calls and
            // may have loaded first. A same-site hook keeps the instruction length,
            // so the bytes after each call still identify the site. Validate those
            // and chain to whatever the call currently points at.
            if (GetModuleHandleW(L"FullBodyFirstPerson.dll"))
                TTPDiagnostic::Trace("Compass: Full Body First Person present, chaining if needed");
            constexpr std::array<std::uint8_t, 12> tail{0x84, 0xC0, 0x74, 0x05, 0x0F, 0x57,
                                                        0xC0, 0xEB, 0x0C, 0x48, 0x8B, 0x03};
            if (callSites.size() > originals.size()) {
                TTPDiagnostic::Trace("Compass: unexpected layout, not patched");
                return false;
            }
            bool chained = false;
            for (std::size_t i = 0; i < callSites.size(); ++i) {
                const auto *code = reinterpret_cast<const std::uint8_t *>(callSites[i]);
                if (code[0] != 0xE8 || !std::equal(tail.begin(), tail.end(), code + 5)) {
                    TTPDiagnostic::Trace("Compass: heading call check failed, not patched");
                    return false;
                }
                const auto target = NativePatch::CallTarget(callSites[i]);
                originals[i] = reinterpret_cast<bool (*)(RE::Actor *)>(target);
                chained = chained || target != predicate;
            }
            // Pivot sites: the vanilla virtual call, then the test/branch after it.
            // A site another mod already changed is left to that mod.
            constexpr std::array<std::uint8_t, 9> pivotBytes{0xFF, 0x90, 0xA8, 0x08, 0x00,
                                                             0x00, 0x84, 0xC0, 0x74};
            std::vector<std::uintptr_t> vanillaPivots;
            for (std::size_t i = 0; i < pivotSites.size(); ++i) {
                const auto *code = reinterpret_cast<const std::uint8_t *>(pivotSites[i]);
                if (!std::equal(pivotBytes.begin() + 6, pivotBytes.end(), code + 6) ||
                    code[pivotBytes.size()] != layout[i].pivotBranch) {
                    TTPDiagnostic::Trace("Compass: pivot check failed, not patched");
                    return false;
                }
                if (std::equal(pivotBytes.begin(), pivotBytes.begin() + 6, code))
                    vanillaPivots.push_back(pivotSites[i]);
                else
                    TTPDiagnostic::Trace("Compass: a pivot call is hooked by another mod; left alone");
            }

            trampoline.Create(96);
            static constexpr std::array<bool (*)(RE::Actor *), 2> thunks{&Heading<0>, &Heading<1>};
            for (std::size_t i = 0; i < callSites.size(); ++i)
                trampoline.WriteCall<5>(callSites[i], reinterpret_cast<std::uintptr_t>(thunks[i]));
            // Pivot falls back to a real virtual call on the actor.
            if (!vanillaPivots.empty())
                NativePatch::RedirectCall6(trampoline, vanillaPivots,
                                           reinterpret_cast<std::uintptr_t>(&Pivot));
            installed = true;
            if (chained)
                TTPDiagnostic::Trace("Compass: chained onto another mod's hook");
            return true;
        }
    };
}
