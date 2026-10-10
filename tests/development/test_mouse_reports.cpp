#include "../../raspikey/MouseReports.h"
#include "../../raspikey/MouseKeys.h"
#include <cassert>
#include <iostream>
int axis(const std::array<uint8_t,9>& r,size_t n){return int16_t(uint16_t(r[n])|(uint16_t(r[n+1])<<8));}
int main() {
    MouseReports mouse;std::array<uint8_t,9> report;
    mouse.Event(EV_REL,REL_X,300);mouse.Event(EV_REL,REL_Y,-500);
    mouse.Event(EV_KEY,BTN_LEFT,1);mouse.Event(EV_KEY,BTN_SIDE,1);
    mouse.Event(EV_KEY,BTN_EXTRA,1);mouse.Event(EV_KEY,BTN_MOUSE+15,1);
    mouse.Event(EV_REL,REL_WHEEL,1);mouse.Event(EV_REL,REL_WHEEL_HI_RES,120);
    mouse.Event(EV_REL,REL_HWHEEL,-1);mouse.Event(EV_REL,REL_HWHEEL_HI_RES,-120);
    assert(mouse.Next(report));assert(report[0]==1&&report[1]==0x19&&report[2]==0x80);
    assert(axis(report,3)==300&&axis(report,5)==-500);
    assert(int8_t(report[7])==1&&int8_t(report[8])==-1);assert(!mouse.Next(report));
    mouse.Event(EV_REL,REL_X,100000);mouse.Event(EV_REL,REL_Y,-100000);
    mouse.Event(EV_REL,REL_WHEEL,300);int x=0,y=0,w=0,count=0;
    while(mouse.Next(report)){x+=axis(report,3);y+=axis(report,5);w+=int8_t(report[7]);++count;}
    assert(x==100000&&y==-100000&&w==300&&count==4);
    mouse.Event(EV_KEY,BTN_LEFT,0);assert(mouse.Next(report)&&!(report[1]&1));
    unsigned long held[(KEY_MAX+8*sizeof(long))/(8*sizeof(long))]={};
    held[BTN_RIGHT/(8*sizeof(long))]|=1UL<<(BTN_RIGHT%(8*sizeof(long)));
    mouse.Resync(held);assert(mouse.Next(report)&&report[1]==2&&report[2]==0);
    assert(axis(report,3)==0&&axis(report,5)==0&&report[7]==0);
    MouseKeys keys;keys.TaskSwitch(true);assert(keys.Keyboard(report));
    assert(report[0]==2&&report[1]==8&&report[3]==43);
    keys.TaskSwitch(false);assert(keys.Keyboard(report)&&report[1]==0&&report[3]==0);
    assert(!keys.Keyboard(report));keys.Event(KEY_LEFTCTRL,1);keys.Event(KEY_A,1);
    assert(keys.Keyboard(report)&&report[1]==1&&report[3]==4);
    keys.Event(KEY_A,2);assert(!keys.Keyboard(report));
    keys.Resync(held);assert(keys.Keyboard(report)&&report[1]==0&&report[3]==0);
    std::array<uint8_t,3> consumer;keys.Event(KEY_BACK,1);
    assert(keys.ConsumerReport(consumer)&&consumer[0]==3&&consumer[1]==0x24&&consumer[2]==2);
    keys.Event(KEY_BACK,0);assert(keys.ConsumerReport(consumer)&&consumer[1]==0&&consumer[2]==0);
    std::cout<<"Mouse reports: motion, all buttons, both wheels, overflow, resync, Task View and extra keys passed\n";
}
