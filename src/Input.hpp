#pragma once
#include "CameraSupport.hpp"
#include "RE/B/ButtonEvent.hpp"
#include "RE/M/MouseMoveEvent.hpp"
#include "RE/T/ThumbstickEvent.hpp"
#include <string_view>
#include <vector>

namespace TrueThirdPerson {
    // Which device is driving the camera and movement right now.
    namespace LookDevice {
        inline bool controller = false;
        inline bool movementController = false;
        inline bool armed = false;
        // Called by both input receivers before native processing, so the result
        // doesn't depend on subscriber order. Gamepad movement and buttons never
        // decide the camera device.
        inline void Observe(const RE::InputEvent *head) {
            armed = WeaponDrawn(RE::PlayerCharacter::GetSingleton());
            for (auto *event = head; event; event = event->next) {
                if (const auto *stick = event->As<RE::ThumbstickEvent>();
                    stick && event->device == RE::INPUT_DEVICE::kGamepad &&
                    (stick->QIDCode() == 11 || stick->QIDCode() == 0x1000B))
                    movementController = true;
                if (const auto *button = event->As<RE::ButtonEvent>();
                    button && event->device == RE::INPUT_DEVICE::kKeyboard && button->QPressed()) {
                    const std::string_view name = button->QUserEvent().c_str();
                    if (name == "Forward" || name == "Back" || name == "Strafe Left" ||
                        name == "Strafe Right")
                        movementController = false;
                }
                if (const auto *mouse = event->As<RE::MouseMoveEvent>();
                    mouse && event->device == RE::INPUT_DEVICE::kMouse &&
                    (mouse->mouseInputX || mouse->mouseInputY)) {
                    controller = false;
                } else if (const auto *stick = event->As<RE::ThumbstickEvent>();
                           stick && event->device == RE::INPUT_DEVICE::kGamepad &&
                           (stick->QIDCode() == 12 || stick->QIDCode() == 0x1000C) &&
                           (stick->xValue != 0.0F || stick->yValue != 0.0F)) {
                    controller = true;
                }
            }
        }
    }

    // Favorites/hotkey equips. While a switch is in progress, facing and ADS
    // handling stand down so the native equip animation isn't disturbed.
    namespace EquipGuard {
        inline thread_local bool processingInput = false;
        inline double until = 0;
        inline bool sprintHeld = false; // "Sprint" key currently down
        inline void (*onButton)(const RE::ButtonEvent *) = nullptr;
        inline void (*onFavorite)() = nullptr;
        inline bool (*filterInput)(const RE::InputEvent *) = nullptr;

        // Removes consumed events from the queue for one receiver call only.
        // The original links are restored afterwards, so other receivers see
        // the full queue.
        struct FilteredQueue {
            std::vector<std::pair<RE::InputEvent *, RE::InputEvent *>> links;
            RE::InputEvent *head = nullptr;
            FilteredQueue(const RE::InputEvent *source, bool (*consume)(const RE::InputEvent *)) {
                // Snapshot first: nothing engine-owned changes until allocation is done.
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
                Profile::Scope timing;
                LookDevice::Observe(head);
                struct Scope {
                    bool previous = std::exchange(processingInput, true);
                    ~Scope() { processingInput = previous; }
                } scope;
                // Tracked even in menus so a release there isn't missed.
                for (auto *e = head; e; e = e->next)
                    if (const auto *b = e->As<RE::ButtonEvent>();
                        b && std::string_view(b->QUserEvent().c_str()) == "Sprint")
                        sprintHeld = b->QPressed();
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
                        }
                    }
                }
                FilteredQueue queue(head, filterInput);
                Profile::Call(original, self, queue.head);
            }
            static bool Install() {
                static_assert(!std::has_virtual_destructor_v<RE::BSInputEventReceiver>);
                return original ||
                       HookSingle(static_cast<RE::BSInputEventReceiver *>(
                                      RE::PlayerControls::GetSingleton()),
                                  0, Thunk, original);
            }
        };
    }
}
