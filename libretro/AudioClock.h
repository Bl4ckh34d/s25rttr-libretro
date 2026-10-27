#pragma once
#include <cstdint>
class AudioClock {
    uint64_t remainder=0;
public:
    void reset() { remainder=0; }
    unsigned advance(uint64_t elapsedNanoseconds) {
        // Loading or a suspended frontend must not produce an audio catch-up burst.
        if(elapsedNanoseconds>250000000) {
            remainder=0;
            return 735;
        }
        const uint64_t scaled=elapsedNanoseconds*44100+remainder;
        remainder=scaled%1000000000;
        return static_cast<unsigned>(scaled/1000000000);
    }
};
