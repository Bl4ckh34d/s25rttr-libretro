#include "libretro.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool environment(unsigned, void*) { return false; }
int main(int argc, char** argv) {
    if(argc != 2) return 2;
    void* handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if(!handle) { std::fprintf(stderr, "FAIL core load: %s\n", dlerror()); return 1; }
#define GET(name) auto name = reinterpret_cast<decltype(&::name)>(dlsym(handle, #name)); if(!name) { std::fprintf(stderr,"FAIL missing %s\n", #name); return 1; }
    GET(retro_api_version); GET(retro_get_system_info); GET(retro_set_environment);
    GET(retro_init); GET(retro_load_game); GET(retro_unload_game); GET(retro_deinit);
    if(retro_api_version() != 1) return 1;
    retro_system_info info{};
    retro_get_system_info(&info);
    if(!info.library_name || !std::strstr(info.library_name,"Roots") || !info.need_fullpath) return 1;
    retro_set_environment(environment);
    retro_init();
    retro_game_info missing{"/nonexistent/S2.EXE", nullptr, 0, nullptr};
    if(retro_load_game(&missing)) { std::fputs("FAIL accepted missing content\n",stderr); return 1; }
    retro_unload_game();
    retro_deinit();
    dlclose(handle);
    std::puts("PASS metadata, invalid-content rejection, host survives unload");
}
