#include "../../raspikey/InputReconnectPolicy.h"
#include <cassert>
#include <iostream>
int main(){
    using Policy=InputReconnectPolicy;
    const std::string keyboard="AA:BB:CC:DD:EE:01",mac="AA:BB:CC:DD:EE:02";
    assert(Policy::Eligible(keyboard,mac,"input-keyboard",true,true,false));
    assert(Policy::Eligible(keyboard,mac,"input-mouse",true,true,false));
    assert(!Policy::Eligible(mac,mac,"input-keyboard",true,true,false));
    assert(!Policy::Eligible(keyboard,mac,"multimedia-player",true,true,false));
    assert(!Policy::Eligible(keyboard,mac,"input-keyboard",false,true,false));
    assert(!Policy::Eligible(keyboard,mac,"input-keyboard",true,false,false));
    assert(!Policy::Eligible(keyboard,mac,"input-keyboard",true,true,true));
    Policy policy;const auto start=Policy::Clock::time_point{};
    assert(policy.Begin(keyboard,true,false,start));
    assert(!policy.Begin(keyboard,true,false,start+std::chrono::seconds(100))); // one in-flight call
    policy.Complete(keyboard);
    assert(!policy.Begin(keyboard,true,false,start+std::chrono::seconds(4)));
    assert(policy.Begin(keyboard,true,false,start+std::chrono::seconds(5)));
    policy.Complete(keyboard);
    assert(!policy.Begin(keyboard,true,false,start+std::chrono::seconds(14)));
    assert(policy.Begin(keyboard,true,false,start+std::chrono::seconds(15)));
    policy.Complete(keyboard);
    assert(!policy.Begin(keyboard,true,true,start+std::chrono::seconds(16))); // recovered
    assert(policy.Begin(keyboard,true,false,start+std::chrono::seconds(17))); // later disconnect
    policy.Pause(keyboard);policy.Complete(keyboard); // late result must not undo manual disconnect
    assert(!policy.Begin(keyboard,true,false,start+std::chrono::hours(1)));
    policy.Resume(keyboard);assert(policy.Begin(keyboard,true,false,start+std::chrono::hours(1)));
    policy.Complete(keyboard);
    assert(!policy.Begin(keyboard,false,false,start+std::chrono::hours(2)));
    std::cout<<"Trusted input retry, output-host exclusion and manual disconnect policy passed\n";
}
