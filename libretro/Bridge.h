#pragma once
#include "libretro.h"
class IVideoDriver;
class VideoDriverLoaderInterface;
IVideoDriver* CreateLibretroVideo(VideoDriverLoaderInterface* callback);
extern retro_hw_render_callback rttr_hw;
extern retro_input_state_t rttr_input;
extern unsigned rttr_width, rttr_height;
void rttr_log(const char* message);
void rttr_message(const char* message);
void* rttr_gl_proc(const char* name);
void ScrollLibretroMap(int dx,int dy);
void SendLibretroGameShortcut(char key);
bool NextLibretroSimulationStep();
