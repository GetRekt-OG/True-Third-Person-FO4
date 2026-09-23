#include "../src/LockMath.hpp"
#include <cassert>
#include <iostream>
int main() {
    for (const auto code : {8U, 0x800U, 0x10800U}) {
        assert(LockMath::MouseControl(true, code, "") == 8);
        assert(LockMath::MouseControl(false, code, "") == -1);
    }
    for (const auto code : {9U, 0x900U, 0x10900U}) {
        assert(LockMath::MouseControl(true, code, "") == 9);
        assert(LockMath::MouseControl(false, code, "") == -1);
    }
    assert(LockMath::MouseControl(true, 2, "") == 2);
    assert(LockMath::MouseControl(true, 0x10002, "") == 2);
    assert(LockMath::MouseControl(true, 777, "Zoom In") == 8);
    assert(LockMath::MouseControl(true, 777, "ZoomOut") == 9);
    assert(LockMath::MouseControl(false, 777, "Zoom In") == -1);
    assert(LockMath::MouseControl(false, 0x56, "Toggle POV") == -1);
    assert(LockMath::MouseControl(true, 0, "Attack") == -1);
    assert(LockMath::MouseControl(true, 1, "Aim") == -1);
    for (int t = -1; t <= 12; ++t)
        assert(LockMath::Melee(t) == (t >= 0 && t <= 6));
    // Target straight ahead of the actor but camera shifted to actor's right:
    // camera must aim LEFT. Character yaw (zero) cannot center this view.
    const auto right = LockMath::CameraCorrection(-40, 200, 0, 0, 1, 0, 12, 1.0F / 60);
    const auto left = LockMath::CameraCorrection(40, 200, 0, 0, 1, 0, 12, 1.0F / 60);
    assert(right[0] < 0 && left[0] > 0 && std::abs(right[0] + left[0]) < 1e-6F);
    assert(right[1] == 0);
    // No constant side offset: converged camera needs zero correction.
    auto aligned = LockMath::CameraCorrection(-40, 200, 30, -40, 200, 30, 12, 1.0F / 60);
    assert(std::abs(aligned[0]) < 1e-6F && std::abs(aligned[1]) < 1e-6F);
    assert(LockMath::CameraCorrection(0, 200, 50, 0, 1, 0, 12, 0.016F)[1] < 0);
    assert(LockMath::CameraCorrection(0, 200, -50, 0, 1, 0, 12, 0.016F)[1] > 0);
    // Left and right selection respect the current target's screen bearing.
    assert(std::isfinite(CombatMath::Score(0.4F, 0.1F, 1)));
    assert(!std::isfinite(CombatMath::Score(-0.4F, 0.1F, 1)));
    assert(std::isfinite(CombatMath::Score(-0.4F, 0.1F, -1)));
    assert(!std::isfinite(CombatMath::Score(0.4F, 0.1F, -1)));
    assert(!std::isfinite(CombatMath::Score(2, 0, 0)));
    assert(LockMath::CameraCorrection(0, 0, 0, 0, 1, 0, 12, 0.016F)[0] == 0);
    const auto noTime = LockMath::CameraCorrection(40, 200, 30, 0, 1, 0, 12, 0);
    assert(noTime[0] == 0 && noTime[1] == 0);
    // Automatic handoff ranks distance across the full range, including behind
    // the view. Manual acquisition retains its forward cone.
    const auto nearBehind = LockMath::SelectionScore(true, 3, 0, 0, 0, -100, 0);
    const auto farAhead = LockMath::SelectionScore(true, 0, 0, 0, 0, 500, 0);
    assert(nearBehind < farAhead && nearBehind == 100);
    assert(!std::isfinite(LockMath::SelectionScore(false, 3, 0, 0, 0, -100, 0)));
    for (float dt : {1.0F / 120, 1.0F / 60, 1.0F / 30}) {
        auto step = LockMath::CameraCorrection(1, -100, 80, 0, 1, 0, 12, dt);
        assert(std::abs(step[0]) <= 3 * dt && std::abs(step[1]) <= 3 * dt);
        assert(step[0] > 0); // shortest path toward the new target
    }
    std::cout
        << "PASS: automatic nearest ranking and bounded handoff,  wheel ID variants, device isolation, melee allowlist, shoulder parallax, centered equilibrium, pitch, directional switching\n";
}
