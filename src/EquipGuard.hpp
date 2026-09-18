#pragma once
#include "CameraSupport.hpp"
#include "EquipLogic.hpp"
#include <string_view>
#include <vector>
namespace TrueThirdPerson::EquipGuard {
    inline thread_local bool processingInput = false;
    inline double until = 0;
    inline void (*onButton)(const RE::ButtonEvent *) = nullptr;
    inline void (*onFavorite)() = nullptr;
    inline bool (*filterInput)(const RE::InputEvent *) = nullptr;
    // Borrow the engine queue only for the duration of this receiver call.
    // Other subscribers receive the original links unchanged.
    struct FilteredQueue {
        std::vector<std::pair<RE::InputEvent *, RE::InputEvent *>> links;
        RE::InputEvent *head = nullptr;
        FilteredQueue(const RE::InputEvent *source, bool (*consume)(const RE::InputEvent *)) {
            // Allocate/snapshot before changing any engine-owned link.
            for (auto *e = const_cast<RE::InputEvent *>(source); e; e = e->next)
                links.emplace_back(e, e->next);
            std::vector<bool> keep;
            keep.reserve(links.size());
            for (const auto &link : links)
                keep.push_back(!consume || !consume(link.first));
            RE::InputEvent *tail = nullptr;
            for (std::size_t i = 0; i < links.size(); ++i) {
                auto *e = links[i].first;
                if (keep[i]) {
                    if (tail)
                        tail->next = e;
                    else
                        head = e;
                    tail = e;
                }
            }
            if (tail)
                tail->next = nullptr;
        }
        ~FilteredQueue() {
            for (auto [e, next] : links)
                e->next = next;
        }
    };
    inline double Now() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    inline void Pause() {
        until = EquipLogic::Extend(until, Now(), 0.75);
    }
    inline bool Active() {
        return processingInput || Now() < until;
    }
    struct InputObserver {
        static inline void (*original)(RE::BSInputEventReceiver *,
                                       const RE::InputEvent *) = nullptr;
        static void Thunk(RE::BSInputEventReceiver *self, const RE::InputEvent *head) {
            struct Scoped {
                bool previous = processingInput;
                Scoped() {
                    processingInput = true;
                }
                ~Scoped() {
                    processingInput = previous;
                }
            } scoped;
            if (Gameplay()) {
                for (auto *e = head; e; e = e->next) {
                    auto *b = e->As<RE::ButtonEvent>();
                    if (!b)
                        continue;
                    if (onButton)
                        onButton(b);
                    if (!b->QJustPressed())
                        continue;
                    const std::string_view name = b->QUserEvent().c_str();
                    if (EquipLogic::FavoriteInput(e->device == RE::INPUT_DEVICE::kKeyboard,
                                                  b->QIDCode(), name)) {
                        Pause();
                        if (onFavorite)
                            onFavorite();
                        REX::LogInformation("Favorites guard: event '{}' code {}", name,
                                            b->QIDCode());
                        TTPDiagnostic::Trace(
                            "Favorites input detected; native equip guard extended");
                    }
                }
            }
            FilteredQueue queue(head, filterInput);
            original(self, queue.head);
        }
        static bool Install() {
            if (original)
                return true;
            auto *c = RE::PlayerControls::GetSingleton();
            if (!c)
                return false;
            auto *receiver = static_cast<RE::BSInputEventReceiver *>(c);
            static_assert(!std::has_virtual_destructor_v<RE::BSInputEventReceiver>);
            HookBatch hooks(*reinterpret_cast<std::uintptr_t **>(receiver));
            hooks.Add(0, Thunk, original);
            if (hooks.Commit())
                return true;
            original = nullptr;
            return false;
        }
    };
} // namespace TrueThirdPerson::EquipGuard
