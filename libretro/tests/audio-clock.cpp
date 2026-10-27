#include "AudioClock.h"
#include <cassert>
#include <cstdio>
static unsigned simulate(unsigned fps) {
    AudioClock clock;
    unsigned samples=0;
    for(unsigned i=1;i<=fps;++i) {
        const uint64_t before=uint64_t(i-1)*1000000000/fps;
        const uint64_t after=uint64_t(i)*1000000000/fps;
        samples+=clock.advance(after-before);
    }
    return samples;
}
int main() {
    assert(simulate(60)==44100);
    assert(simulate(48)==44100); // Music must retain its rate when drawing is slower.
    assert(simulate(30)==44100);
    assert(simulate(75)==44100);
    AudioClock clock;
    unsigned total=0;
    for(int i=0;i<1000;++i)total+=clock.advance(1000000);
    assert(total==44100); // Keep fractional sample time instead of rounding per frame.
    assert(clock.advance(0)==0);
    assert(clock.advance(5000000000ULL)==735); // Do not burst five seconds of audio after loading.
    std::puts("PASS 44.1 kHz at 30/48/60/75 FPS, fractional samples, no pause catch-up burst");
}
