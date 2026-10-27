#pragma once
#include <algorithm>
#include <cmath>
class ControllerAxis {
    double remainder=0;
public:
    int step(int value,double speed) {
        constexpr int deadzone=6000;
        if(std::abs(value)<=deadzone) {remainder=0;return 0;}
        const int magnitude=std::min(std::abs(value),32767)-deadzone;
        remainder+=(value<0?-1:1)*speed*magnitude/(32767-deadzone);
        const int pixels=static_cast<int>(remainder);
        remainder-=pixels;
        return pixels;
    }
};
