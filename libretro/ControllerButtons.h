#pragma once
#include "libretro.h"
#include <cstdint>
inline double controllerMouseSpeed(uint16_t buttons) {
    return buttons & (1u << RETRO_DEVICE_ID_JOYPAD_R2) ? 2.0 : 8.0;
}
struct ControllerShortcut { unsigned button; char key; const char* label; };
inline constexpr ControllerShortcut controllerShortcuts[] = {
    {RETRO_DEVICE_ID_JOYPAD_Y, ' ', "Construction sites (Xbox X)"},
    {RETRO_DEVICE_ID_JOYPAD_X, 'c', "Building names (Xbox Y)"},
    {RETRO_DEVICE_ID_JOYPAD_R, 's', "Productivity percentages (Xbox RB)"},
    {RETRO_DEVICE_ID_JOYPAD_L, 'l', "Minimap (Xbox LB)"},
};
class ControllerButtons {
    uint16_t previous=0;
public:
    template<class Emit> void update(uint16_t buttons, Emit emit) {
        const uint16_t pressed=buttons & ~previous;
        previous=buttons;
        for(const auto& shortcut:controllerShortcuts)
            if(pressed & (1u << shortcut.button)) emit(shortcut.key);
    }
};
