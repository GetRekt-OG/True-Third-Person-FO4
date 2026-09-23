#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace TrueThirdPerson::NativeHookLayout {
    struct Site {
        std::uint64_t function;
        std::size_t offset;
        std::array<std::uint8_t, 2> following;
    };

    constexpr auto PowerCalls(bool og) {
        return std::array{Site{2248454, og ? 0x33U : 0x37U, {0x84, 0xC0}},
                          Site{2248455, 0x36, {0x84, 0xC0}},
                          Site{2248465, 0x4A, {0x84, 0xC0}},
                          Site{2248473, og ? 0x8EU : 0x92U, {0x84, 0xC0}},
                          Site{2248477, og ? 0x24BU : 0x22EU,
                               og ? std::array<std::uint8_t, 2>{0x4C, 0x8D}
                                  : std::array<std::uint8_t, 2>{0xF3, 0x0F}},
                          Site{2248477, og ? 0x6A8U : 0x6C8U, {0x84, 0xC0}},
                          Site{2248482, 0x36, {0x84, 0xC0}}};
    }
    constexpr auto PowerPivots(bool og) {
        return std::array{Site{2248456, og ? 0x494U : 0x448U, {0x84, 0xC0}},
                          Site{2248465, 0xBD, {0x84, 0xC0}},
                          // Rotation adds freeRotation.x after this pivot check. A true
                          // result seeds it with currentYaw instead of the holstered zero.
                          Site{2248473, og ? 0x75U : 0x79U, {0x84, 0xC0}},
                          Site{2248475, og ? 0x57U : 0x5AU, {0x84, 0xC0}}};
    }

    struct CompassSite {
        std::uint64_t function;
        std::size_t callOffset;
        std::size_t pivotOffset;
        std::uint8_t pivotBranch;
    };
    inline constexpr std::array compassOG{CompassSite{2248338, 0x96, 0x7D, 0x0C}};
    inline constexpr std::array compassAE{CompassSite{2248327, 0x2A6, 0x290, 0x09},
                                          CompassSite{2248338, 0x99, 0x80, 0x0C}};
    constexpr std::span<const CompassSite> Compass(bool og) noexcept {
        return og ? std::span<const CompassSite>(compassOG)
                  : std::span<const CompassSite>(compassAE);
    }
} // namespace TrueThirdPerson::NativeHookLayout
