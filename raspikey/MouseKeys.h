#pragma once
#include <array>
#include <algorithm>
#include <linux/input.h>
#include <cstdint>

// Some mouse firmware sends standard keyboard/consumer events for extra buttons.
// Forward those on the mouse's own HID collections, without touching the Apple
// keyboard's filter, remapping settings, or held-key state.
class MouseKeys {
    std::array<bool,KEY_MAX+1> held{};
    bool keyboardChanged=false,consumerChanged=false;
    struct ShortcutState {uint8_t modifiers=0,key=0;bool held=false;};
    std::array<ShortcutState,5> shortcuts{};
public:
    void Shortcut(unsigned source,uint8_t modifiers,uint8_t key,bool pressed) {
        if(source>=shortcuts.size())return;
        auto& saved=shortcuts[source];
        if(saved.held==pressed)return;
        // Keep the press-time chord until release, even across settings changes.
        saved={modifiers,key,pressed};keyboardChanged=true;
    }
    void ReleaseShortcuts(){for(unsigned i=0;i<shortcuts.size();++i)Shortcut(i,0,0,false);}
    void TaskSwitch(bool pressed) {
        Event(KEY_LEFTMETA,pressed);Event(KEY_TAB,pressed);
    }
    static unsigned Usage(unsigned key) {
        static const uint8_t standard[]={
            0,41,30,31,32,33,34,35,36,37,38,39,45,46,42,43,
            20,26,8,21,23,28,24,12,18,19,47,48,40,224,4,22,
            7,9,10,11,13,14,15,51,52,53,225,49,29,27,6,25,
            5,17,16,54,55,56,229,85,226,44,57,58,59,60,61,62,
            63,64,65,66,67,83,71,95,96,97,86,92,93,94,87,89,
            90,91,98,99,0,0,100,68,69
        };
        if(key<sizeof(standard)) return standard[key];
        switch(key) {
            case KEY_KPENTER:return 88; case KEY_RIGHTCTRL:return 228;
            case KEY_KPSLASH:return 84; case KEY_SYSRQ:return 70; case KEY_RIGHTALT:return 230;
            case KEY_HOME:return 74; case KEY_UP:return 82; case KEY_PAGEUP:return 75;
            case KEY_LEFT:return 80; case KEY_RIGHT:return 79; case KEY_END:return 77;
            case KEY_DOWN:return 81; case KEY_PAGEDOWN:return 78; case KEY_INSERT:return 73;
            case KEY_DELETE:return 76; case KEY_PAUSE:return 72;
            case KEY_LEFTMETA:return 227; case KEY_RIGHTMETA:return 231;
            case KEY_COMPOSE:return 101;
            default:return 0;
        }
    }
    static unsigned Consumer(unsigned key) {
        switch(key) {
            case KEY_MUTE:return 0xe2; case KEY_VOLUMEUP:return 0xe9; case KEY_VOLUMEDOWN:return 0xea;
            case KEY_PLAYPAUSE:return 0xcd; case KEY_NEXTSONG:return 0xb5;
            case KEY_PREVIOUSSONG:return 0xb6; case KEY_STOPCD:return 0xb7;
            case KEY_BACK:return 0x224; case KEY_FORWARD:return 0x225;
            default:return 0;
        }
    }
    void Event(unsigned code,int value) {
        if(code>KEY_MAX || value==2 || (!Usage(code)&&!Consumer(code))) return;
        bool next=value!=0;
        if(held[code]==next) return;
        held[code]=next;
        keyboardChanged|=Usage(code)!=0;consumerChanged|=Consumer(code)!=0;
    }
    void Resync(const unsigned long* keys) {
        for(unsigned code=0;code<=KEY_MAX;++code)
            Event(code,(keys[code/(8*sizeof(long))]>>(code%(8*sizeof(long))))&1);
    }
    bool Keyboard(std::array<uint8_t,9>& report) {
        if(!keyboardChanged) return false;
        keyboardChanged=false;report={};report[0]=2;
        size_t slot=3;
        auto add=[&](uint8_t usage) {
            if(!usage)return;
            if(std::find(report.begin()+3,report.begin()+slot,usage)!=report.begin()+slot)return;
            if(slot==report.size()){std::fill(report.begin()+3,report.end(),1);return;}
            report[slot++]=usage;
        };
        for(unsigned key=0;key<=KEY_MAX;++key) if(held[key]) {
            unsigned usage=Usage(key);
            if(usage>=224&&usage<=231) report[1]|=1u<<(usage-224);
            else if(usage) {
                if(slot==report.size()) {std::fill(report.begin()+3,report.end(),1);break;}
                add(uint8_t(usage));
            }
        }
        for(const auto& saved:shortcuts)if(saved.held){report[1]|=saved.modifiers;add(saved.key);}
        return true;
    }
    bool ConsumerReport(std::array<uint8_t,3>& report) {
        if(!consumerChanged)return false;
        consumerChanged=false;unsigned usage=0;
        for(unsigned key=0;key<=KEY_MAX;++key)if(held[key]&&Consumer(key)){usage=Consumer(key);break;}
        report={{3,uint8_t(usage),uint8_t(usage>>8)}};return true;
    }
};
