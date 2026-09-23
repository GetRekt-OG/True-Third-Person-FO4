#include "../src/ADSTurn.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace TrueThirdPerson::ADSTurn;
    Begin(0, 0.35F);
    assert(!Step(1, 0.05F));
    assert(yaw > 0 && yaw < 1);
    for (int i = 0; i < 10; ++i)
        Step(1, 0.05F);
    assert(remaining == 0 && std::abs(yaw - 1) < 0.0001F);
    Cancel();
    assert(!pending && remaining == 0);
    Begin(6.2F, 0.35F);
    Step(0.1F, 0.05F);
    assert(std::abs(Logic::SignedYaw(yaw - 6.2F)) < 0.1F);
    const auto old = yaw;
    Step(1, std::numeric_limits<float>::quiet_NaN());
    assert(yaw == old);
    Cancel();
    assert(!Step(1, 0.05F));
    assert(yaw == old);
    Begin(0, 0.35F);
    Step(1, 1);
    assert(remaining > 0.29F);
}
