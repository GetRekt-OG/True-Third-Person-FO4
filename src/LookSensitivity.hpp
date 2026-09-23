#pragma once
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace TrueThirdPerson::LookSensitivity {
    struct Axes { float x = 1.0F, y = 1.0F; };
    struct Profile {
        Axes holstered, drawn, ads;
        const Axes& Select(bool aiming, bool armed) const noexcept {
            return aiming ? ads : armed ? drawn : holstered;
        }
    };
    inline float Parse(const wchar_t* text, float fallback) noexcept {
        wchar_t* end = nullptr;
        const float value = std::wcstof(text, &end);
        if (end == text || !std::isfinite(value))
            return fallback;
        while (std::iswspace(*end)) ++end;
        if (*end || value < 0.05F || value > 5.0F)
            return fallback;
        return value;
    }
}
