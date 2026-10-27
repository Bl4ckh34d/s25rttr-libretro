#include "Bridge.h"
#include "AudioClock.h"
#include "SimulationClock.h"
#include "SaveStates.h"
#include "SaveStateCodec.h"
#include "AudioSDL.h"
#include "GameManager.h"
#include "GlobalVars.h"
#include "MusicPlayer.h"
#include "QuickStartGame.h"
#include "RttrConfig.h"
#include "Settings.h"
#include "WindowManager.h"
#include "drivers/AudioDriverWrapper.h"
#include "drivers/VideoDriverWrapper.h"
#include "desktops/dskGameInterface.h"
#include "desktops/dskMainMenu.h"
#include "files.h"
#include "languages.h"
#include "ogl/glAllocator.h"
#include "libsiedler2/libsiedler2.h"
#include "s25util/Log.h"
#include "s25util/Socket.h"
#include <boost/filesystem.hpp>
#include <android/log.h>
#include <glad/glad.h>
#include <gl4esinit.h>
#include <array>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

namespace fs = boost::filesystem;
retro_hw_render_callback rttr_hw{};
retro_input_state_t rttr_input = nullptr;
unsigned rttr_width=800, rttr_height=600;
static retro_environment_t environment = nullptr;
static retro_video_refresh_t video = nullptr;
static retro_audio_sample_batch_t audio = nullptr;
static retro_input_poll_t poll = nullptr;
static retro_log_printf_t frontend_log = nullptr;
static std::unique_ptr<GameManager> game;
static fs::path content, resources, userData, oldCwd;
static bool loaded=false, contextReady=false, failed=false, mixerReady=false;
static bool glReady=false;
static bool firstStart=false;
static AudioClock audioClock;
static std::chrono::steady_clock::time_point audioPrevious;
static std::chrono::steady_clock::time_point simulationPrevious, simulationDeadline;
static SimulationPacer simulationPacer;
static float previousFrontendRate=60;
bool NextLibretroSimulationStep() {
    if(std::chrono::steady_clock::now()>=simulationDeadline)return false;
    uint64_t ns;
    if(!simulationPacer.next(ns))return false;
    LibretroSimulationClock::advance(ns);
    return true;
}
static void prepareSimulation() {
    const auto now=std::chrono::steady_clock::now();
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-simulationPrevious).count();
    simulationPrevious=now;
    float rate=60;
    retro_throttle_state throttle{};
    if(dynamic_cast<dskGameInterface*>(WINDOWMANAGER.GetCurrentDesktop())) {
        if(environment(RETRO_ENVIRONMENT_GET_THROTTLE_STATE,&throttle)) {
            if(throttle.mode==RETRO_THROTTLE_FAST_FORWARD)rate=throttle.rate;
        } else {
            bool fast=false;
            if(environment(RETRO_ENVIRONMENT_GET_FASTFORWARDING,&fast)&&fast)rate=0;
        }
    }
    if(rate!=previousFrontendRate) {
        rttr_log(("Frontend simulation rate: "+std::to_string(rate)+" Hz (60 = normal, 0 = unlimited)").c_str());
        previousFrontendRate=rate;
    }
    simulationPacer.begin(static_cast<uint64_t>(std::max<int64_t>(0,elapsed)),rate);
    // Keep input/rendering responsive even with the frontend's unlimited setting.
    simulationDeadline=now+std::chrono::milliseconds(8);
    NextLibretroSimulationStep();
}
static std::vector<uint8_t> suspendedState;
void ResetLibretroResources();
extern "C" void Mix_LibretroRender(void*,int);
extern "C" void Mix_LibretroPauseDevice();
void PauseLibretroMixer() { Mix_LibretroPauseDevice(); }
void rttr_log(const char* message) {
    __android_log_print(ANDROID_LOG_INFO,"RTTR-libretro","%s",message);
    if(frontend_log) frontend_log(RETRO_LOG_INFO,"%s\n",message);
}
void rttr_message(const char* message) {
    rttr_log(message);
    retro_message notification{message,240};
    if(environment)environment(RETRO_ENVIRONMENT_SET_MESSAGE,&notification);
}
void* rttr_gl_proc(const char* name) { return gl4es_GetProcAddress(name); }
static void* hostProc(const char* name) { return reinterpret_cast<void*>(rttr_hw.get_proc_address(name)); }
static void framebufferSize(int* w,int* h) { *w=rttr_width; *h=rttr_height; }
void ScrollLibretroMap(int dx,int dy) {
    if(auto* desktop=dynamic_cast<dskGameInterface*>(WINDOWMANAGER.GetCurrentDesktop()))
        desktop->ScrollView(DrawPoint(dx,dy));
}
void SendLibretroGameShortcut(char key) {
    if(dynamic_cast<dskGameInterface*>(WINDOWMANAGER.GetCurrentDesktop())) {
        KeyEvent event(KeyType::Char);
        event.c=key;
        WINDOWMANAGER.Msg_KeyDown(event);
    }
}

void ConfigureLibretroSettings() {
    if(firstStart) SETTINGS.sound.musicEnabled=true;
    SETTINGS.video.fullscreenSize=SETTINGS.video.windowedSize=VideoMode(rttr_width,rttr_height);
    SETTINGS.video.fullscreen=true;
    SETTINGS.video.framerate=-1;
    SETTINGS.video.guiScale=100;
    SETTINGS.global.use_upnp=false;
    SETTINGS.global.submit_debug_data=2;
    SETTINGS.language.language="de_DE.UTF-8";
    LANGUAGES.setLanguage(SETTINGS.language.language);
}
bool LoadLibretroAudio(AudioDriverWrapper& wrapper) {
    mixerReady=wrapper.LoadDriver(std::make_unique<AudioSDL>(&wrapper));
    return mixerReady;
}
static void stopGame() {
    ClearPendingStateLoad();
    if(game) {
        try { WINDOWMANAGER.CleanUp(); game->Stop(); } catch(const std::exception& e) { rttr_log(e.what()); }
        game.reset();
        setGlobalGameManager(nullptr);
    }
    MUSICPLAYER.Stop();
    ResetLibretroResources();
    AUDIODRIVER.UnloadDriver();
    mixerReady=false;
    VIDEODRIVER.UnloadDriver();
    libsiedler2::setAllocator(nullptr);
    if(glReady) { close_gl4es(); glReady=false; }
    Socket::Shutdown();
    LOG.setWriter(nullptr,LogTarget::File);
    if(!oldCwd.empty()) { boost::system::error_code ec; fs::current_path(oldCwd,ec); oldCwd.clear(); }
}
static void contextDestroy() {
    if(game&&!failed) {
        suspendedState.resize(StateCapacity);
        if(!SerializeGame(suspendedState.data(),suspendedState.size()))suspendedState.clear();
    }
    stopGame();contextReady=false;
}
static void contextReset() { contextReady=true;failed=false; }

static bool startGame() {
    oldCwd=fs::current_path();
    // RttrConfig changes cwd, so keep the change scoped to this core's lifetime.
    setenv("RTTR_PREFIX_DIR",resources.string().c_str(),1);
    if(!RTTRCONFIG.Init()) return false;
    unsetenv("RTTR_PREFIX_DIR");
    RTTRCONFIG.overridePathMapping("GAME",content.parent_path());
    RTTRCONFIG.overridePathMapping("RTTR",resources/"RTTR");
    RTTRCONFIG.overridePathMapping("USERDATA",userData);
    firstStart=!fs::exists(userData/"CONFIG.INI");
    for(auto dir : {s25::folders::config,s25::folders::logs,s25::folders::mapsOwn,s25::folders::mapsPlayed,
                   s25::folders::replays,s25::folders::save,s25::folders::assetsUserOverrides,
                   s25::folders::screenshots,s25::folders::playlists}) fs::create_directories(RTTRCONFIG.ExpandPath(dir));
    LOG.setLogFilepath(RTTRCONFIG.ExpandPath(s25::folders::logs));
    LOG.open();
    set_getprocaddress(hostProc);
    set_getmainfbsize(framebufferSize);
    initialize_gl4es();
    glReady=true;
    if(!Socket::Initialize()) return false;
    libsiedler2::setAllocator(new GlAllocator());
    GLOBALVARS.notdone=true;
    game=std::make_unique<GameManager>(LOG,SETTINGS,VIDEODRIVER,AUDIODRIVER,WINDOWMANAGER);
    setGlobalGameManager(game.get());
    if(!game->Start()) return false;
    ConfigureSaveStates(userData/"libretro");
    audioClock.reset();
    audioPrevious=std::chrono::steady_clock::now();
    simulationPrevious=audioPrevious;
    simulationPacer=SimulationPacer{};
    rttr_log("Game initialized; menu rendering active");
    return true;
}
extern "C" {
RETRO_API unsigned retro_api_version() { return RETRO_API_VERSION; }
RETRO_API void retro_set_environment(retro_environment_t cb) {
    environment=cb;
    static const retro_input_descriptor controls[]={
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"Left click (Xbox A)"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"Right click (Xbox B)"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2,"Precise mouse movement (Xbox RT)"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y,"Construction sites (Xbox X)"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X,"Building names (Xbox Y)"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"Productivity percentages (Xbox RB)"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"Minimap (Xbox LB)"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Back / Escape"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X,"Mouse horizontal"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_Y,"Mouse vertical"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_X,"Map scroll horizontal"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_Y,"Map scroll vertical"},
        {0,0,0,0,nullptr}};
    cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,const_cast<retro_input_descriptor*>(controls));
    bool noGame=false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,&noGame);
    retro_log_callback logging{};
    if(cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE,&logging)) frontend_log=logging.log;
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video=cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio=cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll=cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { rttr_input=cb; }
RETRO_API void retro_init() {}
RETRO_API void retro_deinit() { retro_unload_game(); }
RETRO_API void retro_get_system_info(retro_system_info* info) {
    *info={"Return to the Roots","0.1-prototype","exe",true,false};
}
RETRO_API void retro_get_system_av_info(retro_system_av_info* info) {
    info->geometry={rttr_width,rttr_height,rttr_width,rttr_height,float(rttr_width)/rttr_height};
    info->timing={60.0,44100.0};
}
RETRO_API bool retro_load_game(const retro_game_info* info) {
    if(!info||!info->path||!environment) return false;
    try {
        content=fs::absolute(info->path);
        if(!fs::is_regular_file(content)||!fs::is_directory(content.parent_path()/"DATA")||!fs::is_directory(content.parent_path()/"GFX")) {
            rttr_log("Content must be S2.EXE beside original DATA and GFX directories");return false;
        }
        const char* system=nullptr;
        const char* save=nullptr;
        if(!environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,&system)||!system) return false;
        if(!environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&save)||!save) return false;
        resources=fs::path(system)/"s25rttr";
        userData=fs::path(save)/"s25rttr";
        if(!fs::is_regular_file(resources/"RTTR/assets/base/splash.bmp")) {
            rttr_log("RTTR resources missing from system/s25rttr/RTTR");return false;
        }
        rttr_hw={};
        rttr_hw.context_type=RETRO_HW_CONTEXT_OPENGLES2;
        rttr_hw.context_reset=contextReset;
        rttr_hw.context_destroy=contextDestroy;
        rttr_hw.bottom_left_origin=true;
        environment(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT,nullptr);
        if(!environment(RETRO_ENVIRONMENT_SET_HW_RENDER,&rttr_hw)) return false;
        loaded=true;failed=false;
        uint64_t quirks=RETRO_SERIALIZATION_QUIRK_INCOMPLETE|RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE;
        environment(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS,&quirks);
        return true;
    } catch(const std::exception& e) { rttr_log(e.what());return false; }
}
RETRO_API void retro_unload_game() { suspendedState.clear();if(loaded||game) stopGame();loaded=false;contextReady=false;failed=false; }
RETRO_API void retro_run() {
    if(!loaded||!contextReady||failed) { if(video) video(nullptr,rttr_width,rttr_height,0);return; }
    try {
        if(poll) poll();
        if(!game&&!startGame()) throw std::runtime_error("RTTR initialization failed; inspect RTTR log");
        using BindFramebuffer = void (*)(unsigned,unsigned);
        auto bindFramebuffer=reinterpret_cast<BindFramebuffer>(hostProc("glBindFramebuffer"));
        if(!bindFramebuffer) throw std::runtime_error("Framebuffer entry point unavailable");
        bindFramebuffer(0x8D40,static_cast<unsigned>(rttr_hw.get_current_framebuffer()));
        glViewport(0,0,rttr_width,rttr_height);
        if(!suspendedState.empty()&&dynamic_cast<dskMainMenu*>(WINDOWMANAGER.GetCurrentDesktop())) {
            if(!UnserializeGame(suspendedState.data(),suspendedState.size()))rttr_message("Wiederherstellung nach Grafik-Neustart fehlgeschlagen.");
            std::vector<uint8_t>().swap(suspendedState);
        }
        RunPendingStateLoad();
        prepareSimulation();
        if(!game->Run()) { environment(RETRO_ENVIRONMENT_SHUTDOWN,nullptr);return; }
        // Finish rendering before RetroArch samples this texture in its shared context.
        glFinish();
        if(video) video(RETRO_HW_FRAME_BUFFER_VALID,rttr_width,rttr_height,0);
        if(mixerReady&&audio) {
            const auto now=std::chrono::steady_clock::now();
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-audioPrevious).count();
            audioPrevious=now;
            unsigned remaining=audioClock.advance(static_cast<uint64_t>(std::max<int64_t>(0,elapsed)));
            std::array<int16_t,4096> samples{};
            while(remaining) {
                const unsigned count=std::min(remaining,2048u);
                Mix_LibretroRender(samples.data(),count*2*sizeof(int16_t));
                audio(samples.data(),count);
                remaining-=count;
            }
        }
    } catch(const std::exception& e) { rttr_log(e.what());failed=true; }
}
RETRO_API void retro_reset() {}
RETRO_API void retro_set_controller_port_device(unsigned,unsigned) {}
RETRO_API size_t retro_serialize_size() { return loaded?StateCapacity:0; }
RETRO_API bool retro_serialize(void* data,size_t size) { return game&&!failed&&SerializeGame(data,size); }
RETRO_API bool retro_unserialize(const void* data,size_t size) { return game&&!failed&&UnserializeGame(data,size); }
RETRO_API void retro_cheat_reset() {}
RETRO_API void retro_cheat_set(unsigned,bool,const char*) {}
RETRO_API bool retro_load_game_special(unsigned,const retro_game_info*,size_t) { return false; }
RETRO_API unsigned retro_get_region() { return RETRO_REGION_NTSC; }
RETRO_API void* retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }
}
