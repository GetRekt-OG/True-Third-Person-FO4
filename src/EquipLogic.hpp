#pragma once
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <string>
namespace EquipLogic {
    inline bool FavoriteInput(bool keyboard, std::uint32_t code, std::string_view name) {
        if (keyboard && ((code >= 0x30 && code <= 0x39) || code == 0xBD || code == 0xBB ||
                         (code >= 2 && code <= 13)))
            return true;
        std::string lower(name);
        for (char &c : lower)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return lower.find("favorite") != std::string::npos ||
               lower.find("hotkey") != std::string::npos ||
               lower.find("quickkey") != std::string::npos;
    }
    inline double Extend(double until, double now, double duration) {
        return std::max(until, now + duration);
    }
} // namespace EquipLogic
