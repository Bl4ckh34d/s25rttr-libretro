#include "Bridge.h"
#include "ControllerAxis.h"
#include "ControllerButtons.h"
#include "driver/VideoDriver.h"
#include "driver/VideoDriverLoaderInterface.h"
#include <algorithm>
#include <chrono>
#include <cstring>

class VideoLibretro final : public VideoDriver {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool lastKeys[RETROK_LAST]{};
    unsigned lastLeft = 0;
    bool hadLeft = false;
    bool lastMenu=false;
    ControllerAxis mouseX,mouseY,scrollX,scrollY;
    ControllerButtons buttons;
    int state(unsigned device, unsigned index, unsigned id) const {
        return rttr_input ? rttr_input(0,device,index,id) : 0;
    }
public:
    explicit VideoLibretro(VideoDriverLoaderInterface* callback) : VideoDriver(callback) {}
    const char* GetName() const override { return "libretro"; }
    bool Initialize() override { initialized = true; return true; }
    bool CreateScreen(const std::string&, const VideoMode&, bool) override {
        SetNewSize(VideoMode(rttr_width,rttr_height),Extent(rttr_width,rttr_height));
        isFullscreen_ = true;
        return true;
    }
    bool ResizeScreen(const VideoMode&,bool) override { return true; }
    void DestroyScreen() override {}
    bool SwapBuffers() override { return true; }
    unsigned long GetTickCount() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    }
    OpenGL_Loader_Proc GetLoaderFunction() const override { return rttr_gl_proc; }
    void ListVideoModes(std::vector<VideoMode>& modes) const override { modes.emplace_back(rttr_width,rttr_height); }
    void SetMousePos(Position pos) override { mouse_xy.pos = pos; }
    void* GetMapPointer() const override { return nullptr; }
    void ShowErrorMessage(const std::string& title,const std::string& message) override { rttr_log((title+": "+message).c_str()); }
    KeyEvent GetModKeyState() const override {
        KeyEvent e;
        e.ctrl=state(RETRO_DEVICE_KEYBOARD,0,RETROK_LCTRL)||state(RETRO_DEVICE_KEYBOARD,0,RETROK_RCTRL);
        e.shift=state(RETRO_DEVICE_KEYBOARD,0,RETROK_LSHIFT)||state(RETRO_DEVICE_KEYBOARD,0,RETROK_RSHIFT);
        e.alt=state(RETRO_DEVICE_KEYBOARD,0,RETROK_LALT)||state(RETRO_DEVICE_KEYBOARD,0,RETROK_RALT);
        return e;
    }
    bool MessageLoop() override {
        uint16_t pressed=0;
        for(unsigned id=0;id<16;++id)
            if(state(RETRO_DEVICE_JOYPAD,0,id)) pressed |= uint16_t(1u<<id);
        buttons.update(pressed,SendLibretroGameShortcut);
        int dx=state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_X);
        int dy=state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_Y);
        const double speed=controllerMouseSpeed(pressed);
        dx+=mouseX.step(state(RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X),speed);
        dy+=mouseY.step(state(RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_Y),speed);
        int sx=scrollX.step(state(RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_X),10.0);
        int sy=scrollY.step(state(RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_Y),10.0);
        sx+=5*(!!state(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT)-!!state(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT));
        sy+=5*(!!state(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN)-!!state(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP));
        if(sx||sy) ScrollLibretroMap(sx,sy);
        mouse_xy.pos.x=std::clamp(mouse_xy.pos.x+dx,0,int(rttr_width)-1);
        mouse_xy.pos.y=std::clamp(mouse_xy.pos.y+dy,0,int(rttr_height)-1);
        if(dx||dy) CallBack->Msg_MouseMove(mouse_xy);
        // RetroPad B is the south button: physical Xbox A. RetroPad A is Xbox B.
        bool left=state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_LEFT)||state(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B);
        bool right=state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_RIGHT)||state(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A);
        bool menu=state(RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START);
        if(menu&&!lastMenu) CallBack->Msg_KeyDown(KeyEvent(KeyType::Escape));
        lastMenu=menu;
        if(left!=mouse_xy.ldown) {
            mouse_xy.ldown=left;
            mouse_xy.dbl_click=left&&hadLeft&&GetTickCount()-lastLeft<DOUBLE_CLICK_INTERVAL;
            if(left) { lastLeft=GetTickCount(); hadLeft=true; CallBack->Msg_LeftDown(mouse_xy); }
            else CallBack->Msg_LeftUp(mouse_xy);
        }
        if(right!=mouse_xy.rdown) {
            mouse_xy.rdown=right;
            if(right) CallBack->Msg_RightDown(mouse_xy); else CallBack->Msg_RightUp(mouse_xy);
        }
        if(state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_WHEELUP)) CallBack->Msg_WheelUp(mouse_xy);
        if(state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_WHEELDOWN)) CallBack->Msg_WheelDown(mouse_xy);
        for(unsigned k=0;k<RETROK_LAST;++k) {
            const bool down=state(RETRO_DEVICE_KEYBOARD,0,k)!=0;
            if(down&&!lastKeys[k]) {
                KeyEvent e=GetModKeyState();
                switch(k) {
                case RETROK_ESCAPE:e.kt=KeyType::Escape;break;
                case RETROK_RETURN:e.kt=KeyType::Return;break;
                case RETROK_SPACE:e.kt=KeyType::Space;break;
                case RETROK_LEFT:e.kt=KeyType::Left;break;
                case RETROK_RIGHT:e.kt=KeyType::Right;break;
                case RETROK_UP:e.kt=KeyType::Up;break;
                case RETROK_DOWN:e.kt=KeyType::Down;break;
                case RETROK_BACKSPACE:e.kt=KeyType::Backspace;break;
                case RETROK_TAB:e.kt=KeyType::Tab;break;
                default:if(k>=32&&k<127){e.kt=KeyType::Char;e.c=k;}break;
                }
                if(e.kt!=KeyType::Invalid) CallBack->Msg_KeyDown(e);
            }
            lastKeys[k]=down;
        }
        return true;
    }
};
IVideoDriver* CreateLibretroVideo(VideoDriverLoaderInterface* callback) { return new VideoLibretro(callback); }
