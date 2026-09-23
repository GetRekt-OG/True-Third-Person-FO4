#include "../src/ControllerInput.hpp"
#include <cassert>
#include <limits>
using namespace TrueThirdPerson::ControllerInput;
int main() {
    assert(LockButton(true, 0x80) && LockButton(true, 0x10080));
    assert(!LockButton(false, 0x80) && !LockButton(true, 0x40));
    assert(LookStick(true, 12) && !LookStick(true, 11) && !LookStick(false, 12));
    Flick f;
    assert(f.Update(1, 0, true, true) == 0); // held at acquisition
    assert(f.Update(0, 0, true, true) == 0);
    assert(f.Update(.2F, 0, true, true) == 0); // drift
    assert(f.Update(.7F, .9F, true, true) == 0); // vertical intent
    assert(f.Update(.8F, .1F, true, true) == 1);
    assert(f.Update(1, 0, true, true) == 0); // held
    assert(f.Update(-1, 0, true, true) == 0); // must pass neutral
    f.Update(0, 0, true, true);
    assert(f.Update(-1, 0, true, true) == -1);
    f.Update(0, 0, true, true);
    assert(f.Update(1, 0, true, false) == 0); // cooldown eats flick
    assert(f.Update(1, 0, true, true) == 0);
    f.Update(0, 0, true, true);
    f.Update(0, 0, false, true); // context exit
    assert(f.Update(1, 0, true, true) == 0);
    f.Update(0, 0, true, true);
    f.Update(std::numeric_limits<float>::quiet_NaN(), 0, true, true);
    assert(f.Update(1, 0, true, true) == 0);
}
