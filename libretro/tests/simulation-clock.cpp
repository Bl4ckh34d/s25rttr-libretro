#include "SimulationClock.h"
#include <cassert>
#include <cstdio>
#include <limits>

static uint64_t simulate(unsigned fps,float rate) {
    SimulationPacer pacer;
    uint64_t total=0;
    for(unsigned i=1;i<=fps;++i) {
        pacer.begin(uint64_t(i)*1000000000/fps-uint64_t(i-1)*1000000000/fps,rate);
        uint64_t step;
        while(pacer.next(step)) {assert(step<=10000000);total+=step;}
    }
    return total;
}
int main() {
    for(unsigned fps:{30u,48u,60u,180u}) {
        assert(simulate(fps,60)==1000000000);
        assert(simulate(fps,120)==2000000000);
        assert(simulate(fps,180)==3000000000);
        assert(simulate(fps,90)==1500000000);
    }
    SimulationPacer pacer;uint64_t step;
    pacer.begin(10000000,180);assert(pacer.next(step)&&step==10000000);
    pacer.begin(10000000,60);assert(pacer.next(step)&&step==10000000);assert(!pacer.next(step));
    pacer.begin(5000000000ULL,180);uint64_t afterPause=0;
    while(pacer.next(step))afterPause+=step;
    assert(afterPause<60000000); // No fast-forward burst after a menu or load.
    pacer.begin(1,0);for(int i=0;i<1000;++i)assert(pacer.next(step)&&step==10000000);
    assert(simulate(60,std::numeric_limits<float>::quiet_NaN())==1000000000);
    assert(simulate(60,-1)==1000000000);
    assert(simulate(60,std::numeric_limits<float>::infinity())==1000000000);
    std::puts("PASS simulation rates, fractions, unlimited mode, release, pauses and invalid rates");
}
