#include "../src/Logic.hpp"
#include <cassert>
#include <cstdio>

using namespace TrueThirdPerson;
constexpr float pi = std::numbers::pi_v<float>;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

static bool Near(float a, float b, float eps = 0.00001F) {
    return std::abs(a - b) < eps;
}

static void Angles() {
    assert(Near(Logic::Normalize(0), 0));
    assert(Near(Logic::Normalize(-pi / 2), 3 * pi / 2));
    assert(Near(Logic::Normalize(5 * pi / 2), pi / 2));
    assert(Near(Logic::SignedYaw(3 * pi / 2), -pi / 2));
    assert(Near(Logic::SignedYaw(pi / 3), pi / 3));
    for (float camera : {-2.0F, -0.4F, 0.5F, 2.6F})
        assert(Near(Logic::SignedYaw(camera), camera));
}

static void FacingBlend() {
    const float one = Facing::Blend(0, pi / 2, 1.0F / 60);
    assert(one > 0 && one < pi / 2);
    // Shortest path across the 0/2pi seam.
    assert(std::abs(Logic::SignedYaw(Facing::Blend(2 * pi - 0.01F, 0.01F, 0.01F) -
                                     (2 * pi - 0.01F))) < 0.02F);
    // Same result at 60 and 120 fps.
    float a = 0, b = 0;
    for (int i = 0; i < 12; ++i)
        a = Facing::Blend(a, pi / 2, 1.0F / 60);
    for (int i = 0; i < 24; ++i)
        b = Facing::Blend(b, pi / 2, 1.0F / 120);
    assert(std::abs(a - b) < 1e-5F);
    assert(std::abs(a - pi / 2) < 0.014F);
    // Timed blend lands exactly on target.
    float yaw = 0, remaining = 0.18F;
    while (remaining > 0) {
        const float dt = std::min(0.016F, remaining);
        yaw = Facing::Blend(yaw, pi / 2, dt, remaining);
        remaining = std::max(0.0F, remaining - dt);
    }
    assert(std::abs(yaw - pi / 2) < 1e-5F);
    assert(Facing::Blend(1, 2, 0) == 1);
    assert(Facing::Blend(1, kNaN, 0.01F) == 1);
}

static void MoveTurn() {
    using Facing::MoveTurn;
    assert(MoveTurn(0, 1, 0, 0, 0.016F) == 0);
    assert(MoveTurn(1, 0, 0, 0, 0.016F) > 0);
    assert(MoveTurn(-1, 0, 0, 0, 0.016F) < 0);
    assert(std::abs(MoveTurn(0, -1, 0, 0, 1)) <= 0.3F);
    assert(std::abs(MoveTurn(0, 1, 0.02F, 2 * pi - 0.02F, 0.05F) - 0.04F) < 0.00001F);
    assert(MoveTurn(0, 0, 1, 0, 0.01F) == 0);
    assert(MoveTurn(1, 0, 0, 0, -1) == 0);
    assert(MoveTurn(1, 0, 0, 0, kNaN) == 0);
    float yaw = 0;
    for (int i = 0; i < 100; ++i)
        yaw += MoveTurn(1, 0, 0, yaw, 0.016F);
    assert(std::abs(yaw - pi / 2) < 0.00001F);
}

static void IdleRotation() {
    struct Point {
        float x, y, z;
    };
    struct Output {
        Point movementRotation, rotationSpeed;
        float movementSpeed;
        Point targetAngle;
    };
    assert(Facing::Stationary(0, 0, 0));
    assert(!Facing::Stationary(.002F, 0, 0));
    assert(!Facing::Stationary(0, 0, .1F));
    assert(!Facing::Stationary(0, 0, -.1F));
    assert(!Facing::Stationary(kNaN, 0, 0));
    Output out{{1, 2, 3}, {4, 5, 6}, 0, {7, 8, 9}};
    assert(Facing::ClearCameraTurn(out, 2));
    assert(out.rotationSpeed.z == 0 && out.targetAngle.z == 2);
    assert(out.rotationSpeed.x == 4 && out.rotationSpeed.y == 5);
    assert(out.targetAngle.x == 7 && out.targetAngle.y == 8);
    assert(out.movementRotation.x == 1 && out.movementRotation.y == 2 &&
           out.movementRotation.z == 3);
    assert(out.movementSpeed == 0);
    assert(!Facing::ClearCameraTurn(out, kNaN));
    assert(out.targetAngle.z == 2);
}

static void AdsTurn() {
    using namespace ADSTurn;
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
    Step(1, kNaN);
    assert(yaw == old);
    Cancel();
    assert(!Step(1, 0.05F));
    assert(yaw == old);
    Begin(0, 0.35F);
    Step(1, 1);
    assert(remaining > 0.29F);
    Cancel();
}

static void Favorites() {
    for (unsigned c = 0x30; c <= 0x39; ++c)
        assert(EquipLogic::FavoriteInput(true, c, ""));
    assert(EquipLogic::FavoriteInput(true, 0xBD, ""));
    assert(EquipLogic::FavoriteInput(true, 0xBB, ""));
    assert(EquipLogic::FavoriteInput(false, 99, "QuickKey1"));
    assert(EquipLogic::FavoriteInput(false, 99, "Favorites"));
    assert(EquipLogic::FavoriteInput(false, 99, "HOTKEY3"));
    assert(!EquipLogic::FavoriteInput(true, 0x57, "Forward"));
    assert(!EquipLogic::FavoriteInput(true, 0x41, "Strafe Left"));
    assert(!EquipLogic::FavoriteInput(true, 0x53, "Back"));
    assert(!EquipLogic::FavoriteInput(true, 0x44, "Strafe Right"));
    assert(!EquipLogic::FavoriteInput(false, 0x31, "Attack"));
    // The equip guard never gets shorter.
    assert(EquipLogic::Extend(10.75, 10.1, 0.25) == 10.75);
    assert(EquipLogic::Extend(10.75, 10.6, 0.25) == 10.85);
}

static void TargetLock() {
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

    // Target straight ahead of the actor with the camera on the right shoulder:
    // the camera has to turn left. Actor yaw alone can't center this.
    const auto right = LockMath::CameraCorrection(-40, 200, 0, 0, 1, 0, 12, 1.0F / 60);
    const auto left = LockMath::CameraCorrection(40, 200, 0, 0, 1, 0, 12, 1.0F / 60);
    assert(right[0] < 0 && left[0] > 0 && std::abs(right[0] + left[0]) < 1e-6F);
    assert(right[1] == 0);
    const auto aligned = LockMath::CameraCorrection(-40, 200, 30, -40, 200, 30, 12, 1.0F / 60);
    assert(std::abs(aligned[0]) < 1e-6F && std::abs(aligned[1]) < 1e-6F);
    assert(LockMath::CameraCorrection(0, 200, 50, 0, 1, 0, 12, 0.016F)[1] < 0);
    assert(LockMath::CameraCorrection(0, 200, -50, 0, 1, 0, 12, 0.016F)[1] > 0);
    assert(LockMath::CameraCorrection(0, 0, 0, 0, 1, 0, 12, 0.016F)[0] == 0);
    const auto noTime = LockMath::CameraCorrection(40, 200, 30, 0, 1, 0, 12, 0);
    assert(noTime[0] == 0 && noTime[1] == 0);

    // Left/right switching is relative to the current target's bearing.
    assert(std::isfinite(LockMath::Score(0.4F, 0.1F, 1)));
    assert(!std::isfinite(LockMath::Score(-0.4F, 0.1F, 1)));
    assert(std::isfinite(LockMath::Score(-0.4F, 0.1F, -1)));
    assert(!std::isfinite(LockMath::Score(0.4F, 0.1F, -1)));
    assert(!std::isfinite(LockMath::Score(2, 0, 0)));

    // Automatic handoff ranks by distance, even behind the view.
    const auto nearBehind = LockMath::SelectionScore(true, 3, 0, 0, 0, -100, 0);
    const auto farAhead = LockMath::SelectionScore(true, 0, 0, 0, 0, 500, 0);
    assert(nearBehind < farAhead && nearBehind == 100);
    assert(!std::isfinite(LockMath::SelectionScore(false, 3, 0, 0, 0, -100, 0)));
    for (float dt : {1.0F / 120, 1.0F / 60, 1.0F / 30}) {
        const auto step = LockMath::CameraCorrection(1, -100, 80, 0, 1, 0, 12, dt);
        assert(std::abs(step[0]) <= 3 * dt && std::abs(step[1]) <= 3 * dt);
        assert(step[0] > 0);
    }
}

static void TwistMath() {
    using namespace Twist;
    const float limit = pi / 3;
    // Running to the right of the camera within the limit: the body goes there.
    assert(std::abs(Logic::SignedYaw(BodyTarget(0, 0.5F, 0, limit) - 0.5F)) < 1e-5F);
    // Past the limit: held at the limit.
    assert(std::abs(Logic::SignedYaw(BodyTarget(0, 2.0F, 0, limit) - limit)) < 1e-5F);
    assert(std::abs(Logic::SignedYaw(BodyTarget(0, -2.0F, 0, limit) + limit)) < 1e-5F);
    // Straight back: the side the body is on, no flipping.
    assert(std::abs(Logic::SignedYaw(BodyTarget(0, pi, 0.3F, limit) - limit)) < 1e-5F);
    assert(std::abs(Logic::SignedYaw(BodyTarget(0, pi, -0.3F, limit) + limit)) < 1e-5F);
    // A +90 degree turn sends forward (+Y) to +X.
    const Mat3 identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const auto turned = TurnLocal(identity, identity, pi / 2);
    const float x = turned[0][1], y = turned[1][1]; // image of local +Y
    assert(std::abs(x - 1) < 1e-5F && std::abs(y) < 1e-5F);
    // With any parent, the node's world rotation is the old one turned about world up.
    const Mat3 parent = Multiply(YawRotation(0.7F), Mat3{{{1, 0, 0}, {0, 0.8F, -0.6F}, {0, 0.6F, 0.8F}}});
    const Mat3 local = YawRotation(-0.4F);
    const auto world = Multiply(parent, TurnLocal(parent, local, 0.5F));
    const auto expected = Multiply(YawRotation(0.5F), Multiply(parent, local));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            assert(std::abs(world[i][j] - expected[i][j]) < 1e-5F);
    // Transposed storage: stored = true^T, world = local * parent, and the row
    // formula local' = local * parent * R^T * parent^T gives world' = world * R^T,
    // i.e. the same turn about world up.
    {
        const auto P = Transpose(parent), L = Transpose(local);
        const auto Lp = Multiply(Multiply(Multiply(L, P), Transpose(YawRotation(0.5F))), Transpose(P));
        const auto W = Multiply(Lp, P);
        const auto expect = Transpose(Multiply(YawRotation(0.5F), Multiply(parent, local)));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                assert(std::abs(W[i][j] - expect[i][j]) < 1e-5F);
    }
    float value = 0;
    for (int i = 0; i < 60; ++i)
        value = Approach(value, 1, 12, 1.0F / 60);
    assert(value > 0.99F && value <= 1);
}

static void Numbers() {
    using Logic::ParseNumber;
    assert(ParseNumber(L"0.0000", 5, 0, 10) == 0);
    assert(ParseNumber(L" 2.5 ", 5, 0, 10) == 2.5F);
    assert(ParseNumber(L"11", 5, 0, 10) == 5 && ParseNumber(L"-1", 5, 0, 10) == 5);
    assert(ParseNumber(L"", 5, 0, 10) == 5 && ParseNumber(L"abc", 5, 0, 10) == 5);
}

static void Steer() {
    using namespace DrawSteer;
    assert(Sector(0, 1) == 0 && Sector(1, 1) == 1 && Sector(1, 0) == 2 && Sector(1, -1) == 3);
    assert(Sector(0, -1) == 4 && Sector(-1, -1) == 5 && Sector(-1, 0) == 6 && Sector(-1, 1) == 7);
    // Rotating clockwise by 90 degrees turns forward into right.
    const auto r = Rotate(0, 1, std::numbers::pi_v<float> / 2);
    assert(std::abs(r[0] - 1) < 1e-5F && std::abs(r[1]) < 1e-5F);
    assert(std::abs(std::atan2(Rotate(1, 0, -0.6F)[0], Rotate(1, 0, -0.6F)[1]) -
                    (std::numbers::pi_v<float> / 2 - 0.6F)) < 1e-5F);
    // A constant +35 degree offset is learned away.
    float correction = 0;
    const float offset = 0.61F;
    for (int i = 0; i < 120; ++i)
        correction = Learn(correction, correction + offset);
    assert(std::abs(correction + offset) < 0.01F);
    assert(Learn(0.2F, 2.0F) == 0.2F); // wall slides and nonsense are ignored
    assert(Learn(0, -100) == 0 && Learn(0.85F, -1.0F) == 0.9F);
}

static void Keys() {
    using namespace LockKeys;
    // Defaults: middle mouse and right stick click.
    assert(Matches(Pick(keyboardMouse, 0), Device::Mouse, 2));
    assert(Matches(Pick(keyboardMouse, 0), Device::Mouse, 0x10002));
    assert(!Matches(Pick(keyboardMouse, 0), Device::Keyboard, 2));
    assert(Matches(Pick(gamepad, 0), Device::Gamepad, 0x80));
    assert(Matches(Pick(gamepad, 0), Device::Gamepad, 0x10080));
    assert(!Matches(Pick(gamepad, 0), Device::Gamepad, 0x40));
    assert(!Matches(Pick(gamepad, 0), Device::Mouse, 0x80));
    // Keyboard keys are VK codes and never match with the device bit.
    assert(Matches(Pick(keyboardMouse, 3), Device::Keyboard, 'B'));
    assert(!Matches(Pick(keyboardMouse, 3), Device::Keyboard, 0x10000 | 'B'));
    // Last entry is "None"; out of range falls back to the default.
    constexpr auto none = Pick(keyboardMouse, static_cast<int>(std::size(keyboardMouse)) - 1);
    assert(none.device == Device::None && !Matches(none, Device::None, 0));
    assert(Pick(gamepad, 99).code == 0x80 && Pick(gamepad, -1).code == 0x80);
    // Favorites use 1-9, 0, - and =; none of those can be a lock key.
    for (const auto key : keyboardMouse)
        assert(key.device != Device::Keyboard ||
               !((key.code >= '0' && key.code <= '9') || key.code == 0xBD || key.code == 0xBB));
}

static void Controller() {
    using namespace ControllerInput;
    assert(LookStick(true, 12) && !LookStick(true, 11) && !LookStick(false, 12));
    Flick f;
    assert(f.Update(1, 0, true, true) == 0); // held when the lock was acquired
    assert(f.Update(0, 0, true, true) == 0);
    assert(f.Update(.2F, 0, true, true) == 0); // drift
    assert(f.Update(.7F, .9F, true, true) == 0); // mostly vertical
    assert(f.Update(.8F, .1F, true, true) == 1);
    assert(f.Update(1, 0, true, true) == 0); // still held
    assert(f.Update(-1, 0, true, true) == 0); // has to pass neutral
    f.Update(0, 0, true, true);
    assert(f.Update(-1, 0, true, true) == -1);
    f.Update(0, 0, true, true);
    assert(f.Update(1, 0, true, false) == 0); // cooldown eats the flick
    assert(f.Update(1, 0, true, true) == 0);
    f.Update(0, 0, true, true);
    f.Update(0, 0, false, true); // left lock context
    assert(f.Update(1, 0, true, true) == 0);
    f.Update(0, 0, true, true);
    f.Update(kNaN, 0, true, true);
    assert(f.Update(1, 0, true, true) == 0);
}

static void Steering() {
    const auto step = [](ControllerSteering &s, float angle, float dt) {
        return s.Update(std::sin(angle), std::cos(angle), dt);
    };
    ControllerSteering f;
    assert(std::abs(step(f, 1, 1.f / 60) - 1) < .0001f); // no stale heading on start
    step(f, 0, 1);                                       // long gap reseeds
    float h = step(f, 1, .065f);
    assert(std::abs(h - (1 - std::exp(-1.f))) < .0001f);
    ControllerSteering a, b;
    step(a, 0, .01f);
    step(b, 0, .01f);
    for (int i = 0; i < 30; i++)
        step(a, 1, 1.f / 30);
    for (int i = 0; i < 120; i++)
        step(b, 1, 1.f / 120);
    assert(std::abs(step(a, 1, .001f) - step(b, 1, .001f)) < .0001f);
    f.Reset();
    step(f, 3.13f, .01f);
    h = step(f, -3.13f, .01f);
    assert(std::abs(h) > 3); // shortest arc, not through forward
    f.Update(0, 0, .01f);
    assert(std::abs(step(f, -1, .01f) + 1) < .0001f); // neutral resets
    f.Update(kNaN, 0, .01f);
    assert(std::abs(step(f, 1, .01f) - 1) < .0001f);
}

static void Sensitivity() {
    using namespace LookSensitivity;
    assert(Parse(L"0.35", 1) == 0.35F);
    assert(Parse(L" 2.5 ", 1) == 2.5F);
    for (const auto *bad : {L"", L"nan", L"inf", L"0", L"-1", L"6", L"1junk", L"0,5"})
        assert(Parse(bad, 0.5F) == 0.5F);
    Profile controller{{1, 2}, {3, .5F}, {4, 5}};
    assert(controller.Select(false, false).y == 2);
    assert(controller.Select(false, true).y == .5F);
    assert(controller.Select(true, true).x == 4);
    assert(controller.Select(true, false).x == 4); // ADS wins
    Profile mouse;
    assert(mouse.Select(false, true).y == 1);
}

static void InputScope() {
    enum class Weapon { Holstered, Drawn, Sheathing };
    struct Actor {
        Weapon weaponState : 3;
    };
    Actor actor{Weapon::Holstered};
    bool spoof = true, native = false;
    {
        WeaponInputScope outer(actor, spoof, native, Weapon::Drawn, true);
        assert(actor.weaponState == Weapon::Drawn && !spoof && native);
        {
            WeaponInputScope inner(actor, spoof, native, Weapon::Drawn, false);
            assert(actor.weaponState == Weapon::Drawn && native);
        }
        assert(actor.weaponState == Weapon::Drawn && !spoof && native);
    }
    assert(actor.weaponState == Weapon::Holstered && spoof && !native);
    {
        WeaponInputScope guard(actor, spoof, native, Weapon::Drawn, true);
        actor.weaponState = Weapon::Sheathing; // handler really changed it
    }
    assert(actor.weaponState == Weapon::Sheathing && spoof && !native);
    spoof = false;
    actor.weaponState = Weapon::Drawn;
    {
        WeaponInputScope guard(actor, spoof, native, Weapon::Drawn, false);
        actor.weaponState = Weapon::Holstered;
    }
    assert(actor.weaponState == Weapon::Holstered && !spoof && !native);
}

int main() {
    Angles();
    FacingBlend();
    MoveTurn();
    IdleRotation();
    AdsTurn();
    Favorites();
    TargetLock();
    Keys();
    Steer();
    Numbers();
    TwistMath();
    Controller();
    Steering();
    Sensitivity();
    InputScope();
    std::puts("logic: ok");
}
