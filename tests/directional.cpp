#include "../src/DirectionalMovement.hpp"
#include <cassert>
#include <limits>
#include <numbers>
int main() {
    using DirectionalMovement::TurnDelta;
    constexpr float pi = std::numbers::pi_v<float>;
    assert(TurnDelta(0, 1, 0, 0, 0.016F) == 0);
    assert(TurnDelta(1, 0, 0, 0, 0.016F) > 0);
    assert(TurnDelta(-1, 0, 0, 0, 0.016F) < 0);
    assert(std::abs(TurnDelta(0, -1, 0, 0, 1)) <= 0.3F);
    assert(std::abs(TurnDelta(0, 1, 0.02F, 2 * pi - 0.02F, 0.05F) - 0.04F) < 0.00001F);
    assert(TurnDelta(0, 0, 1, 0, 0.01F) == 0);
    assert(TurnDelta(1, 0, 0, 0, -1) == 0);
    assert(TurnDelta(1, 0, 0, 0, std::numeric_limits<float>::quiet_NaN()) == 0);
    float yaw = 0;
    for (int i = 0; i < 100; ++i)
        yaw += TurnDelta(1, 0, 0, yaw, 0.016F);
    assert(std::abs(yaw - pi / 2) < 0.00001F);
}
