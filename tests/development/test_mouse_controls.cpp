#include "../../raspikey/MouseConfiguration.h"
#include "../../raspikey/HidppProtocol.h"
#include "../../raspikey/MouseKeys.h"
#include "../../raspikey/MouseKeyboardMerge.h"
#include <cassert>
#include <iostream>
int main() {
    auto packet=Hidpp::Request(5,3,{0,0xc4,3,0,0});
    assert(packet[0]==0x11&&packet[1]==0xff&&packet[2]==5&&packet[3]==0x3e&&packet[5]==0xc4);
    assert(Hidpp::Reply(packet.data(),packet.size(),5,3));
    packet[1]=0;assert(Hidpp::Reply(packet.data(),packet.size(),5,3));
    packet[3]=0x31;assert(!Hidpp::Reply(packet.data(),packet.size(),5,3));
    packet={0x11,0,5,0,0,0xc4,0,0x53};bool pressed=false;
    assert(Hidpp::Toggle(packet.data(),20,5,pressed)&&pressed);
    packet[5]=0;assert(Hidpp::Toggle(packet.data(),20,5,pressed)&&!pressed);
    assert(!Hidpp::Toggle(packet.data(),7,5,pressed));
    packet={0x11,0,0xff,5,0x3e,2};assert(Hidpp::Error(packet.data(),20,5,3));
    MouseConfiguration defaults;auto json=MouseConfigurationJson(defaults);
    auto config=ParseMouseConfiguration(json.dump());assert(config.scrollMode=="free"&&config.buttons[4].action==1&&config.buttons[4].key==70);
    for(int caseNo=0;caseNo<6;++caseNo) {
        auto bad=json;
        if(caseNo==0)bad["scrollMode"]="invalid";
        if(caseNo==1)bad["smartShift"]=1;
        if(caseNo==2)bad["buttons"]["scrollToggle"]["modifiers"]=-1;
        if(caseNo==3)bad["buttons"]["scrollToggle"]["key"]=102;
        if(caseNo==4)bad["buttons"]["scrollToggle"]["action"]="run-shell";
        if(caseNo==5)bad["buttons"]["scrollToggle"]["modifiers"]=uint64_t(4294967296);
        bool rejected=false;try{ParseMouseConfiguration(bad.dump());}catch(const std::exception&){rejected=true;}assert(rejected);
    }
    MouseKeys keys;std::array<uint8_t,9> report;
    keys.Shortcut(4,3,23,true);assert(keys.Keyboard(report)&&report[1]==3&&report[3]==23);
    keys.Shortcut(4,0,70,true);assert(!keys.Keyboard(report)); // hold keeps original chord
    keys.Shortcut(2,1,6,true);assert(keys.Keyboard(report)&&report[1]==3&&report[3]==6&&report[4]==23);
    keys.Shortcut(4,0,70,false);assert(keys.Keyboard(report)&&report[1]==1&&report[3]==6&&report[4]==0);
    keys.Event(KEY_LEFTCTRL,1);keys.Event(KEY_C,1);assert(keys.Keyboard(report)&&report[1]==1&&report[3]==6&&report[4]==0);
    keys.ReleaseShortcuts();assert(keys.Keyboard(report)&&report[1]==1&&report[3]==6);
    keys.Event(KEY_C,0);keys.Event(KEY_LEFTCTRL,0);assert(keys.Keyboard(report)&&report[1]==0&&report[3]==0);
    auto combined=MergeMouseKeyboard({{{2,1,0,6}},{{2,2,0,23,6}}});
    assert(combined[1]==3&&combined[3]==6&&combined[4]==23&&combined[5]==0);
    combined=MergeMouseKeyboard({{{2,1,0,4,5,6,7,8,9}},{{2,2,0,10}},{{2,4,0,11}}});
    assert(combined[1]==7&&combined[3]==1&&combined[8]==1);
    std::cout<<"HID++ replies/notifications, strict settings, shortcut overlap/releases and keyboard merging passed\n";
}
