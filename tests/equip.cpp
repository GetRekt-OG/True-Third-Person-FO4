#include "../src/EquipLogic.hpp"
#include <cassert>
#include <iostream>
int main() {
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
    assert(EquipLogic::Extend(10.75, 10.1, 0.25) == 10.75);
    assert(EquipLogic::Extend(10.75, 10.6, 0.25) == 10.85);
    std::cout << "PASS: favorite bindings, movement exclusions, non-shortening equip guard\n";
}
