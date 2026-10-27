#include "SaveStateCodec.h"
#include <cassert>
#include <cstdio>
int main() {
    SavedState state;
    state.gameFrame=12345;state.player=0;state.x=-217;state.y=813;state.zoom=1.5f;state.hud=7;state.paused=true;
    state.payload={0,1,2,255,4,5};
    std::vector<uint8_t> bytes(256,0xcc);
    assert(encodeState(state,bytes.data(),bytes.size()));
    SavedState decoded;
    assert(decodeState(bytes.data(),bytes.size(),decoded));
    assert(decoded.payload==state.payload&&decoded.gameFrame==12345&&decoded.x==-217&&decoded.y==813);
    assert(decoded.zoom==1.5f&&decoded.hud==7&&decoded.paused);
    bytes.back()=99; // Frontend padding is outside the payload.
    assert(decodeState(bytes.data(),bytes.size(),decoded));
    bytes[48]^=1;
    assert(!decodeState(bytes.data(),bytes.size(),decoded));
    bytes[48]^=1;bytes[24]^=1;
    assert(!decodeState(bytes.data(),bytes.size(),decoded));
    assert(!decodeState(bytes.data(),47,decoded));
    assert(!encodeState(state,bytes.data(),49));
    bytes.assign(256,0);
    assert(!decodeState(bytes.data(),bytes.size(),decoded));
    std::puts("PASS save envelope round-trip, camera/HUD, checksum, truncation and capacity checks");
}
