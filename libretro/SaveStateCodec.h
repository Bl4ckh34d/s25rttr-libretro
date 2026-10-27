#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cstring>
#include <cmath>
#include <utility>
#include <zlib.h>
inline constexpr size_t StateCapacity=16*1024*1024;
struct SavedState {
    uint32_t gameFrame=0,player=0,hud=0;
    int32_t x=0,y=0;
    float zoom=1.f;
    bool paused=false;
    std::vector<uint8_t> payload;
};
inline void stateWrite32(uint8_t* p,uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8)); }
inline uint32_t stateRead32(const uint8_t* p) { uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(p[i])<<(i*8);return n; }
inline bool encodeState(const SavedState& state,void* output,size_t size) {
    if(!output||state.payload.empty()||state.payload.size()>StateCapacity-48||size<48+state.payload.size())return false;
    auto* bytes=static_cast<uint8_t*>(output);
    std::memset(bytes,0,size);
    std::memcpy(bytes,"RTTRLR01",8);
    stateWrite32(bytes+8,static_cast<uint32_t>(state.payload.size()));
    stateWrite32(bytes+16,state.gameFrame);stateWrite32(bytes+20,state.player);
    stateWrite32(bytes+24,static_cast<uint32_t>(state.x));stateWrite32(bytes+28,static_cast<uint32_t>(state.y));
    uint32_t zoom;std::memcpy(&zoom,&state.zoom,4);stateWrite32(bytes+32,zoom);
    stateWrite32(bytes+36,state.hud);stateWrite32(bytes+40,state.paused?1:0);
    std::memcpy(bytes+48,state.payload.data(),state.payload.size());
    stateWrite32(bytes+12,crc32(0,bytes+16,static_cast<uInt>(32+state.payload.size())));
    return true;
}
inline bool decodeState(const void* input,size_t size,SavedState& output) {
    if(!input||size<48)return false;
    auto* bytes=static_cast<const uint8_t*>(input);
    if(std::memcmp(bytes,"RTTRLR01",8))return false;
    const uint32_t length=stateRead32(bytes+8);
    if(!length||length>StateCapacity-48||length>size-48)return false;
    if(stateRead32(bytes+12)!=crc32(0,bytes+16,32+length))return false;
    SavedState result;
    result.gameFrame=stateRead32(bytes+16);result.player=stateRead32(bytes+20);
    result.x=static_cast<int32_t>(stateRead32(bytes+24));result.y=static_cast<int32_t>(stateRead32(bytes+28));
    const uint32_t zoom=stateRead32(bytes+32);std::memcpy(&result.zoom,&zoom,4);
    result.hud=stateRead32(bytes+36);result.paused=stateRead32(bytes+40)!=0;
    if(result.player>=8||result.hud>7||!std::isfinite(result.zoom)||result.zoom<0.1f||result.zoom>10.f)return false;
    result.payload.assign(bytes+48,bytes+48+length);
    output=std::move(result);
    return true;
}
