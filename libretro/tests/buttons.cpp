#include "ControllerButtons.h"
#include <cassert>
#include <cstdio>
#include <string>
int main() {
    auto button=[](unsigned id){return uint16_t(1u<<id);};
    assert(controllerMouseSpeed(0)==8.0);
    assert(controllerMouseSpeed(button(RETRO_DEVICE_ID_JOYPAD_R2))==2.0);
    assert(controllerMouseSpeed(button(RETRO_DEVICE_ID_JOYPAD_L2))==8.0);
    ControllerButtons buttons;
    std::string keys;
    auto emit=[&](char key){keys+=key;};
    const uint16_t x=button(RETRO_DEVICE_ID_JOYPAD_Y); // Physical Xbox X.
    buttons.update(x,emit);
    for(int i=0;i<120;++i) buttons.update(x,emit);
    assert(keys==" "); // Holding must not toggle the overlay repeatedly.
    buttons.update(0,emit);buttons.update(x,emit);
    assert(keys=="  "); // A new press toggles it back.
    buttons.update(0,emit);
    buttons.update(button(RETRO_DEVICE_ID_JOYPAD_X)|button(RETRO_DEVICE_ID_JOYPAD_R)|button(RETRO_DEVICE_ID_JOYPAD_L),emit);
    assert(keys=="  csl"); // Xbox Y, RB, LB; simultaneous presses remain distinct.
    buttons.update(button(RETRO_DEVICE_ID_JOYPAD_L2)|button(RETRO_DEVICE_ID_JOYPAD_R2),emit);
    assert(keys=="  csl"); // Triggers never emit gameplay shortcuts.
    std::puts("PASS R2 precision, L2 unchanged, single-press toggles, release/repress, simultaneous shortcuts");
}
