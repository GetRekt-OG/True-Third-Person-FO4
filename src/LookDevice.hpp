#pragma once
#include "CameraSupport.hpp"
#include "RE/M/MouseMoveEvent.hpp"
#include "RE/T/ThumbstickEvent.hpp"

namespace TrueThirdPerson::LookDevice {
    inline bool controller = false;
    inline bool armed = false;
    // Observe both receivers before native processing; neither subscriber order
    // nor a gamepad movement/button event should select the camera input device.
    inline void Observe(const RE::InputEvent* head) {
        armed = WeaponDrawn(RE::PlayerCharacter::GetSingleton());
        for (auto* event = head; event; event = event->next) {
            if (const auto* mouse = event->As<RE::MouseMoveEvent>();
                mouse && event->device == RE::INPUT_DEVICE::kMouse &&
                (mouse->mouseInputX || mouse->mouseInputY)) {
                controller = false;
            } else if (const auto* stick = event->As<RE::ThumbstickEvent>();
                       stick && event->device == RE::INPUT_DEVICE::kGamepad &&
                       (stick->QIDCode() == 12 || stick->QIDCode() == 0x1000C) &&
                       (stick->xValue != 0.0F || stick->yValue != 0.0F)) {
                controller = true;
            }
        }
    }
}
