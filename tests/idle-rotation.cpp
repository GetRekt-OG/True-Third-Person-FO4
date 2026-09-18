#include "../src/IdleRotation.hpp"
#include <cassert>
#include <limits>
struct Point {
    float x, y, z;
};
struct Output {
    Point movementRotation, rotationSpeed;
    float movementSpeed;
    Point targetAngle;
};
int main() {
    using namespace IdleRotation;
    assert(Stationary(0, 0, 0));
    assert(!Stationary(.002F, 0, 0));
    assert(!Stationary(0, 0, .1F));
    assert(!Stationary(0, 0, -.1F));
    assert(!Stationary(std::numeric_limits<float>::quiet_NaN(), 0, 0));
    Output out{{1, 2, 3}, {4, 5, 6}, 0, {7, 8, 9}};
    assert(ClearCameraTurn(out, 2));
    assert(out.rotationSpeed.z == 0 && out.targetAngle.z == 2);
    assert(out.rotationSpeed.x == 4 && out.rotationSpeed.y == 5);
    assert(out.targetAngle.x == 7 && out.targetAngle.y == 8);
    assert(out.movementRotation.x == 1 && out.movementRotation.y == 2 &&
           out.movementRotation.z == 3);
    assert(out.movementSpeed == 0);
    assert(!ClearCameraTurn(out, std::numeric_limits<float>::quiet_NaN()));
    assert(out.targetAngle.z == 2);
}
