#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

// Keep native clock types/layout, but advance the local client and server together.
struct LibretroSimulationClock : std::chrono::steady_clock {
    inline static time_point current = std::chrono::steady_clock::now();
    static time_point now() noexcept { return current; }
    static void advance(uint64_t ns) { current += std::chrono::nanoseconds(ns); }
};

class SimulationPacer {
    uint64_t pending=0;
    long double fraction=0;
    bool unlimited=false;
public:
    void begin(uint64_t elapsedNs,float frontendRate) {
        // A load, suspended frontend or open menu must not create catch-up work.
        if(elapsedNs>250000000) {elapsedNs=16666667;fraction=0;}
        if(!std::isfinite(frontendRate)||frontendRate<0)frontendRate=60;
        unlimited=frontendRate==0;
        const long double scaled=elapsedNs*(static_cast<long double>(frontendRate)/60)+fraction;
        // Work is also bounded by a real-time budget in the caller.
        pending=static_cast<uint64_t>(std::min(scaled,1000000000.0L));
        fraction=scaled-std::floor(scaled);
    }
    bool next(uint64_t& ns) {
        if(unlimited) {ns=10000000;return true;}
        if(!pending)return false;
        ns=std::min(pending,uint64_t(10000000));pending-=ns;return true;
    }
};
