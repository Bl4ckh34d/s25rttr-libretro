// Real GLES smoke test, runs on the TV without replacing its foreground app.
#include "libretro.h"
#include "SaveStateCodec.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <unistd.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
struct Event {unsigned frame;int x,y,left,right,scrollX,scrollY;};
static std::vector<Event> events;
static unsigned currentFrame=0;
static bool testFastForward=false;
static float fastForwardRate=60;
static const char* systemDir;
static const char* saveDir;
static retro_hw_render_callback hw{};
static unsigned frames=0, audioFrames=0, audible=0, duplicates=0;
static GLuint frontendFramebuffer=0;
static unsigned wrongFramebuffer=0;
static uintptr_t framebuffer() { return frontendFramebuffer; }
static retro_proc_address_t procedure(const char* name) {
    auto p=eglGetProcAddress(name);
    return p ? reinterpret_cast<retro_proc_address_t>(p) : reinterpret_cast<retro_proc_address_t>(dlsym(RTLD_DEFAULT,name));
}
static void logger(enum retro_log_level,const char* fmt,...) {
    va_list args;va_start(args,fmt);std::vfprintf(stdout,fmt,args);va_end(args);std::fflush(stdout);
}
static bool environment(unsigned cmd,void* data) {
    switch(cmd) {
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE:
        if(!testFastForward)return false;
        *static_cast<retro_throttle_state*>(data)={unsigned(fastForwardRate==60?RETRO_THROTTLE_NONE:RETRO_THROTTLE_FAST_FORWARD),fastForwardRate};return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:*static_cast<bool*>(data)=testFastForward&&fastForwardRate!=60;return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:static_cast<retro_log_callback*>(data)->log=logger;return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*static_cast<const char**>(data)=systemDir;return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*static_cast<const char**>(data)=saveDir;return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        hw=*static_cast<retro_hw_render_callback*>(data);
        hw.get_current_framebuffer=framebuffer;hw.get_proc_address=procedure;
        *static_cast<retro_hw_render_callback*>(data)=hw;return hw.context_type==RETRO_HW_CONTEXT_OPENGLES2;
    default:return false;
    }
}
static void video(const void* pixels,unsigned,unsigned,size_t) {
    if(pixels==RETRO_HW_FRAME_BUFFER_VALID) ++frames;else ++duplicates;
    GLint bound=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&bound);
    if(GLuint(bound)!=frontendFramebuffer) ++wrongFramebuffer;
}
static size_t audio(const int16_t* samples,size_t count) {
    audioFrames+=count;
    for(size_t i=0;i<count*2;i++) if(samples[i]) {++audible;break;}
    return count;
}
static void poll() {}
static int16_t input(unsigned,unsigned device,unsigned index,unsigned id) {
    for(const auto& e:events) if(e.frame==currentFrame) {
        if(device==RETRO_DEVICE_MOUSE) {
            if(id==RETRO_DEVICE_ID_MOUSE_X)return e.x;
            if(id==RETRO_DEVICE_ID_MOUSE_Y)return e.y;
        }
        if(device==RETRO_DEVICE_JOYPAD) {
            if(id==RETRO_DEVICE_ID_JOYPAD_B)return e.left;
            if(id==RETRO_DEVICE_ID_JOYPAD_A)return e.right;
        }
        if(device==RETRO_DEVICE_ANALOG&&index==RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            return id==RETRO_DEVICE_ID_ANALOG_X?e.scrollX:e.scrollY;
    }
    return 0;
}
int main(int argc,char** argv) {
    if(argc!=6&&argc!=7) {std::puts("core content system-directory save-directory screenshot.rgba [events.txt]");return 2;}
    if(argc==7) {
        FILE* script=std::fopen(argv[6],"r");if(!script)return 2;
        Event e{};while(std::fscanf(script,"%u %d %d %d %d %d %d",&e.frame,&e.x,&e.y,&e.left,&e.right,&e.scrollX,&e.scrollY)==7)events.push_back(e);
        std::fclose(script);
    }
    systemDir=argv[3];saveDir=argv[4];
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(!eglInitialize(display,nullptr,nullptr)) {std::printf("FAIL EGL init %x\n",eglGetError());return 1;}
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;
    if(!eglChooseConfig(display,attrs,&config,1,&count)||!count) return 1;
    EGLint pbuffer[]={EGL_WIDTH,800,EGL_HEIGHT,600,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,pbuffer);
    EGLint ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxattrs);
    if(!eglMakeCurrent(display,surface,surface,context)) return 1;
    std::printf("GPU %s\n",glGetString(GL_RENDERER));
    GLuint frontendTexture=0;
    if(std::getenv("RTTR_TEST_FRONTEND_FBO")) {
        glGenTextures(1,&frontendTexture);glBindTexture(GL_TEXTURE_2D,frontendTexture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1024,1024,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glGenFramebuffers(1,&frontendFramebuffer);glBindFramebuffer(GL_FRAMEBUFFER,frontendFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,frontendTexture,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 1;
        glBindTexture(GL_TEXTURE_2D,0);
    }
    void* core=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!core){std::printf("FAIL %s\n",dlerror());return 1;}
#define LOAD(name) auto name=reinterpret_cast<decltype(&::name)>(dlsym(core,#name));if(!name)return 1;
    LOAD(retro_init);LOAD(retro_deinit);LOAD(retro_set_environment);LOAD(retro_set_video_refresh);
    LOAD(retro_set_audio_sample_batch);LOAD(retro_set_input_poll);LOAD(retro_set_input_state);
    LOAD(retro_load_game);LOAD(retro_run);LOAD(retro_unload_game);LOAD(retro_reset);
    LOAD(retro_serialize_size);LOAD(retro_serialize);LOAD(retro_unserialize);
    retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);
    retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
    retro_game_info game{argv[2],nullptr,0,nullptr};
    if(!retro_load_game(&game)){std::puts("FAIL content rejected");return 1;}
    const bool testStates=std::getenv("RTTR_TEST_STATES");
    testFastForward=std::getenv("RTTR_TEST_FAST_FORWARD");
    std::vector<double> simulationRates;
    unsigned lastGF=0;
    std::chrono::steady_clock::time_point phaseStart;
    bool fastForwardPassed=false;
    const bool testContext=std::getenv("RTTR_TEST_CONTEXT_RESET");
    const bool testReset=std::getenv("RTTR_TEST_RESET");
    if(testReset&&(!testStates||testContext))return 2;
    if(testStates && !retro_serialize_size()) {std::puts("FAIL save states unsupported");return 1;}
    hw.context_reset();
    std::vector<uint8_t> saved;
    SavedState savedMetadata;
    unsigned savedFrame=0,loadFrame=0;
    bool restored=false;
    const char* inputState=std::getenv("RTTR_TEST_STATE_INPUT");
    const char* outputState=std::getenv("RTTR_TEST_STATE_OUTPUT");
    if(inputState) {
        FILE* stateFile=std::fopen(inputState,"rb");if(!stateFile)return 1;
        saved.resize(retro_serialize_size());
        auto read=std::fread(saved.data(),1,saved.size(),stateFile);std::fclose(stateFile);
        if(read!=saved.size()||!decodeState(saved.data(),saved.size(),savedMetadata))return 1;
        savedFrame=savedMetadata.gameFrame;loadFrame=savedFrame+10;
        events.clear();
    }
    std::vector<double> durations;
    const int intervalUs=std::getenv("RTTR_TEST_INTERVAL_US")?std::clamp(std::atoi(std::getenv("RTTR_TEST_INTERVAL_US")),1000,100000):16667;
    std::chrono::steady_clock::time_point audioMeasureStart;
    unsigned audioMeasureSamples=0;
    const unsigned requestedFrames=std::getenv("RTTR_TEST_FRAMES")?std::clamp(std::atoi(std::getenv("RTTR_TEST_FRAMES")),480,3600):480;
    for(unsigned i=0;i<requestedFrames;i++) {
        // Exclude startup and the splash-to-menu asset upload from steady-state timing.
        if(i==300) {audioMeasureStart=std::chrono::steady_clock::now();audioMeasureSamples=audioFrames;}
        currentFrame=i;
        auto start=std::chrono::steady_clock::now();retro_run();
        if(testFastForward && i>=600 && (i-600)%150==0 && i<=1350) {
            const auto measuredAt=std::chrono::steady_clock::now();
            std::vector<uint8_t> stateBytes(retro_serialize_size());SavedState state;
            if(!retro_serialize(stateBytes.data(),stateBytes.size())||!decodeState(stateBytes.data(),stateBytes.size(),state)) {
                std::puts("FAIL fast-forward test needs an active game");return 1;
            }
            const auto now=std::chrono::steady_clock::now();
            if(i>600) {
                const double rate=(state.gameFrame-lastGF)/std::chrono::duration<double>(measuredAt-phaseStart).count();
                simulationRates.push_back(rate);
                std::printf("SIMULATION requested %.1f Hz: %.2f gameframes/second\n",fastForwardRate,rate);std::fflush(stdout);
            }
            lastGF=state.gameFrame;phaseStart=now;
            if(i==750)fastForwardRate=120;
            if(i==900)fastForwardRate=180;
            if(i==1050)fastForwardRate=60;
            if(i==1200)fastForwardRate=0;
            if(i==1350) {
                const auto base=simulationRates[0];
                fastForwardPassed=base>1&&simulationRates[1]/base>1.7&&simulationRates[1]/base<2.3
                    &&simulationRates[2]/base>2.6&&simulationRates[2]/base<3.4
                    &&simulationRates[3]/base>0.8&&simulationRates[3]/base<1.2&&simulationRates[4]>simulationRates[2];
                std::puts(fastForwardPassed?"PASS frontend fast-forward ratios and release":"FAIL frontend fast-forward ratios or release");
                fastForwardRate=60;
            }
        }
        if(inputState && i==330) {
            if(!retro_unserialize(saved.data(),saved.size())) {std::puts("FAIL persisted state rejected after restart");return 1;}
        }
        if(testStates && !inputState && i==700) {
            saved.resize(retro_serialize_size());
            if(!retro_serialize(saved.data(),saved.size())) {std::puts("FAIL save active game");return 1;}
            SavedState state;
            if(!decodeState(saved.data(),saved.size(),state))return 1;
            savedFrame=state.gameFrame;
            savedMetadata=state;
            if(outputState) {
                FILE* stateFile=std::fopen(outputState,"wb");if(!stateFile)return 1;
                auto written=std::fwrite(saved.data(),1,saved.size(),stateFile);std::fclose(stateFile);
                if(written!=saved.size())return 1;
            }
            std::printf("STATE saved GF=%u camera=%d,%d\n",savedFrame,state.x,state.y);
            auto corrupt=saved;corrupt[48]^=1;
            if(retro_unserialize(corrupt.data(),corrupt.size())) {std::puts("FAIL corrupt state accepted");return 1;}
        }
        if(testStates && !inputState && i==850) {
            std::vector<uint8_t> later(saved.size());SavedState state;
            if(!retro_serialize(later.data(),later.size())||!decodeState(later.data(),later.size(),state)||state.gameFrame<=savedFrame)
                {std::puts("FAIL simulation did not advance after rejected state");return 1;}
            loadFrame=state.gameFrame;
            if(testReset) {
                retro_reset();
            } else if(testContext) {
                savedFrame=state.gameFrame;savedMetadata=state;loadFrame=savedFrame+10;
                hw.context_destroy();
                eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
                eglDestroyContext(display,context);
                context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxattrs);
                if(!eglMakeCurrent(display,surface,surface,context))return 1;
                hw.context_reset();
            } else if(!retro_unserialize(saved.data(),saved.size())) {std::puts("FAIL load state rejected");return 1;}
        }
        if(testReset&&i==851) {
            std::vector<uint8_t> afterReset(saved.size());
            if(retro_serialize(afterReset.data(),afterReset.size())) {
                std::puts("FAIL reset left the previous game running");return 1;
            }
            std::puts("PASS reset stopped the previous game");
        }
        if(testReset&&i==1200) {
            if(!retro_unserialize(saved.data(),saved.size())) {
                std::puts("FAIL load after reset rejected");return 1;
            }
        }
        if(testStates && i>(inputState?330u:(testReset?1200u:850u)) && !restored) {
            std::vector<uint8_t> later(saved.size());SavedState state;
            if(retro_serialize(later.data(),later.size())&&decodeState(later.data(),later.size(),state)) {
                if(state.gameFrame<savedFrame||state.gameFrame>=loadFrame) {std::puts("FAIL game frame not restored");return 1;}
                if(state.x!=savedMetadata.x||state.y!=savedMetadata.y||state.zoom!=savedMetadata.zoom||state.hud!=savedMetadata.hud)
                    {std::puts("FAIL camera/zoom/HUD not restored");return 1;}
                std::printf("PASS game frame restored %u -> %u (saved %u)\n",loadFrame,state.gameFrame,savedFrame);
                restored=true;
            }
        }
        auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        durations.push_back(us/1000.0);
        if(us<intervalUs)usleep(intervalUs-us);
    }
    const double audioSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-audioMeasureStart).count();
    const double measuredAudioRate=(audioFrames-audioMeasureSamples)/audioSeconds;
    std::printf("AUDIO_CLOCK measured %.1f samples/second at requested %d us/frame\n",measuredAudioRate,intervalUs);
    std::vector<unsigned char> pixels(800*600*4);
    glBindFramebuffer(GL_FRAMEBUFFER,frontendFramebuffer);
    glReadPixels(0,0,800,600,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    unsigned nonblack=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]||pixels[i+1]||pixels[i+2])++nonblack;
    FILE* image=std::fopen(argv[5],"wb");if(image){std::fwrite(pixels.data(),1,pixels.size(),image);std::fclose(image);}
    std::printf("RESULT frames=%u duplicates=%u audio_frames=%u audible_batches=%u nonblack=%u\n",frames,duplicates,audioFrames,audible,nonblack);
    std::printf("FRAMEBUFFER incorrect frames=%u\n",wrongFramebuffer);
    auto begin=durations.end()-180;
    std::sort(begin,durations.end());
    std::printf("FRAME_TIME last 180 frames median=%.2fms p95=%.2fms max=%.2fms\n",begin[90],begin[171],begin[179]);
    retro_unload_game();if(hw.context_destroy)hw.context_destroy();retro_deinit();dlclose(core);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
    if(testStates&&!restored)std::puts("FAIL state was not restored, including graphics context recovery when requested");
    return frames>300&&nonblack>10000&&wrongFramebuffer==0&&(!testFastForward||fastForwardPassed)&&(testStates?restored:(testFastForward||(measuredAudioRate>43000&&measuredAudioRate<45200)))?0:1;
}
