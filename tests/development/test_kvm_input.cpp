#include "../../raspikey/KvmInputState.h"
#include <cassert>
#include <iostream>
int main(){
    const uint8_t apple[10]={1,5,0,69,0,0,0,0,0,2};
    assert(KvmWireSize(false,apple,10)==9);assert(KvmWireSize(false,apple,9)==9);assert(KvmWireSize(true,apple,9)==9);
    KvmInputState state;
    state.keyboard={5,0,69};assert(state.Trigger(5,69));assert(!state.Trigger(5,69));
    state.Switched();assert(state.Suppressed());state.keyboard={0x50,0,69};assert(!state.Trigger(5,69));assert(state.Suppressed());
    state.keyboard={};assert(!state.Trigger(5,69));assert(state.Suppressed());assert(!state.Suppressed());
    state.keyboard={0x50,0,69};assert(state.Trigger(5,69)); // right modifiers work
    state.keyboard={7,0,69};assert(!state.Trigger(5,69)); // exact modifiers
    state.keyboard={1,0,4,5};state.auxiliary={8,0,5,6};auto report=state.MergedKeyboard();assert(report[0]==9&&report[2]==4&&report[3]==5&&report[4]==6&&report[5]==0);
    state.keyboard={1,0,4,5,6,7,8,9};state.auxiliary={8,0,10};report=state.MergedKeyboard();assert(report[0]==9);for(size_t i=2;i<8;++i)assert(report[i]==1);
    state.keyboard={};state.auxiliary={};state.buttons=1;state.Switched();assert(state.Suppressed());state.buttons=0;state.consumer=0xcd;assert(state.Suppressed());state.consumer=0;assert(state.Suppressed());assert(!state.Suppressed());
    std::cout<<"KVM chord, merged reports and held-input quarantine tests passed\n";
}
