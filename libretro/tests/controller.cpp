#include "ControllerAxis.h"
#include <cassert>
#include <cstdio>
int main() {
    ControllerAxis axis;
    for(int i=0;i<120;i++) assert(axis.step(5999,8)==0);
    assert(axis.step(32767,8)==8);
    assert(axis.step(-32768,8)==-8);
    assert(axis.step(32767,2)==2);
    int total=0;
    for(int i=0;i<120;i++) total+=axis.step(7000,2);
    assert(total>=8&&total<=9); // Fine movement must accumulate below one pixel.
    for(int i=0;i<120;i++) assert(axis.step(0,8)==0);
    std::puts("PASS deadzone, full range, precision, subpixel accumulation, no idle drift");
}
