#include "../src/FacingBlend.hpp"
#include <cassert>
#include <iostream>
#include <limits>
int main() {
    constexpr float pi = std::numbers::pi_v<float>;
    const float one = FacingBlend::Step(0, pi / 2, 1.0F / 60);
    assert(one > 0 && one < pi / 2);
    assert(std::abs(Logic::SignedYaw(FacingBlend::Step(2 * pi - 0.01F, 0.01F, 0.01F) -
                                     (2 * pi - 0.01F))) < 0.02F);
    float a = 0, b = 0;
    for (int i = 0; i < 12; ++i)
        a = FacingBlend::Step(a, pi / 2, 1.0F / 60);
    for (int i = 0; i < 24; ++i)
        b = FacingBlend::Step(b, pi / 2, 1.0F / 120);
    assert(std::abs(a - b) < 1e-5F);
    assert(std::abs(a - pi / 2) < 0.014F);
    float yaw = 0, remaining = 0.18F;
    while (remaining > 0) {
        float dt = std::min(0.016F, remaining);
        yaw = FacingBlend::Step(yaw, pi / 2, dt, remaining);
        remaining = std::max(0.0F, remaining - dt);
    }
    assert(std::abs(yaw - pi / 2) < 1e-5F);
    assert(FacingBlend::Step(1, 2, 0) == 1);
    assert(FacingBlend::Step(1, std::numeric_limits<float>::quiet_NaN(), 0.01F) == 1);
    std::cout
        << "PASS: gradual turn, shortest path, frame-rate consistency, exact return completion, zero/invalid delta\n";
}
