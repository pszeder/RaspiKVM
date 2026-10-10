#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>

inline size_t KvmWireSize(bool mouse,const uint8_t* report,size_t size){
    // A1644/A1314 filters append an internal Fn/Eject byte. The existing USB
    // gadget truncates it; Bluetooth needs the same explicit wire format.
    return !mouse&&report&&size>=9&&report[0]==1?9:size;
}

inline std::array<uint8_t,8> KvmMousePayload(const uint8_t* payload,bool reverseScroll){
    std::array<uint8_t,8> result{};
    std::copy_n(payload,8,result.begin());
    if(reverseScroll)for(size_t i:{size_t(6),size_t(7)}){
        int wheel=int8_t(result[i]);
        result[i]=uint8_t(wheel==-128?127:-wheel);
    }
    return result;
}

// Physical state survives a switch, but is quarantined until every held input
// has been released. Never replay a switch chord or a held drag on the new host.
struct KvmInputState {
    std::array<uint8_t,8> keyboard{}, auxiliary{};
    uint16_t buttons=0, consumer=0, auxiliaryConsumer=0;
    bool quarantine=false, chordHeld=false;
    static uint8_t Modifiers(uint8_t value){return uint8_t((value | (value>>4)) & 15);}
    bool Held() const {
        return buttons||consumer||auxiliaryConsumer||
            std::any_of(keyboard.begin(),keyboard.end(),[](uint8_t b){return b!=0;})||
            std::any_of(auxiliary.begin(),auxiliary.end(),[](uint8_t b){return b!=0;});
    }
    bool Chord(uint8_t modifiers,uint8_t key) const {
        return Modifiers(keyboard[0])==modifiers&&std::find(keyboard.begin()+2,keyboard.end(),key)!=keyboard.end();
    }
    bool Trigger(uint8_t modifiers,uint8_t key) {
        bool held=Chord(modifiers,key), edge=held&&!chordHeld;
        chordHeld=held;return edge&&!quarantine;
    }
    void Switched(){quarantine=Held();}
    bool Suppressed(){if(!quarantine)return false;if(!Held())quarantine=false;return true;}
    std::array<uint8_t,8> MergedKeyboard() const {
        std::array<uint8_t,8> report{};report[0]=keyboard[0]|auxiliary[0];size_t slot=2;bool overflow=false;
        for(const auto* source:{&keyboard,&auxiliary})for(size_t i=2;i<8;++i) {
            uint8_t key=(*source)[i];if(!key)continue;
            if(key<=3){overflow=true;continue;}
            if(std::find(report.begin()+2,report.begin()+slot,key)!=report.begin()+slot)continue;
            if(slot==8){overflow=true;continue;}report[slot++]=key;
        }
        if(overflow)std::fill(report.begin()+2,report.end(),1);
        return report;
    }
};
