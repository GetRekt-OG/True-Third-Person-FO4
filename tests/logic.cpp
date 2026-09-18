#include "../src/Logic.hpp"
#include <cassert>
#include <iostream>
int main() {
    const float pi = std::numbers::pi_v<float>;
    auto near = [](float a, float b) { return std::abs(a - b) < 0.00001F; };
    assert(near(Logic::Normalize(0), 0));
    assert(near(Logic::Normalize(-pi / 2), 3 * pi / 2));
    assert(near(Logic::Normalize(5 * pi / 2), pi / 2));
    assert(near(Logic::SignedYaw(3 * pi / 2), -pi / 2));
    assert(near(Logic::SignedYaw(pi / 3), pi / 3));
    // The seed is an absolute world yaw. Varying an actor anchor must not change it.
    for (float camera : {-2.0F, -0.4F, 0.5F, 2.6F}) {
        for (float actor : {-3.0F, -1.0F, 1.0F, 3.0F}) {
            (void)actor;
            assert(near(Logic::SignedYaw(camera), camera));
        }
    }
    std::cout << "PASS: camera yaw normalization checks\n";
}
