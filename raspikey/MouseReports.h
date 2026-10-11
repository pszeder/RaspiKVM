#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <linux/input.h>

// Independent mouse endpoint: ID 1 mouse, ID 2 keyboard, ID 3 consumer control.
// Preserve 16-bit motion and detent scrolling; never forward both coarse and
// high-resolution wheel events (Linux emits both for the same wheel movement).
class MouseReports {
    int64_t x=0, y=0, wheel=0, pan=0;
    uint16_t buttons=0;
    bool changed=false;
public:
    static int Button(unsigned code) {
        return code>=BTN_MOUSE&&code<BTN_MOUSE+16 ? int(code-BTN_MOUSE) : -1;
    }
    void Event(unsigned type, unsigned code, int value) {
        if(type==EV_REL) {
            if(code==REL_X) {x+=value; changed=true;}
            if(code==REL_Y) {y+=value; changed=true;}
            if(code==REL_WHEEL) {wheel+=value; changed=true;}
            if(code==REL_HWHEEL) {pan+=value; changed=true;}
        } else if(type==EV_KEY && Button(code)>=0 && value!=2) {
            const uint16_t mask=uint16_t(1u<<Button(code));
            uint16_t next=value ? buttons|mask : buttons&~mask;
            changed|=buttons!=next; buttons=next;
        }
    }
    void Resync(const unsigned long* keys) {
        x=y=wheel=pan=0; buttons=0; changed=true;
        for(unsigned code=BTN_MOUSE;code<BTN_MOUSE+16;++code)
            if(keys[code/(8*sizeof(long))] & (1UL<<(code%(8*sizeof(long))))) Event(EV_KEY,code,1);
    }
    bool Next(std::array<uint8_t,9>& report) {
        if(!changed) return false;
        auto take=[](int64_t& total,int limit) {
            int amount=int(std::max<int64_t>(-limit,std::min<int64_t>(limit,total)));
            total-=amount; return amount;
        };
        int dx=take(x,32767),dy=take(y,32767),dw=take(wheel,127),dp=take(pan,127);
        report={{1,uint8_t(buttons),uint8_t(buttons>>8),uint8_t(dx),uint8_t(uint16_t(dx)>>8),uint8_t(dy),uint8_t(uint16_t(dy)>>8),uint8_t(dw),uint8_t(dp)}};
        changed=x||y||wheel||pan;
        return true;
    }
};
