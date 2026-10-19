#pragma once
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <string>

// Used only by the background Bluetooth loop, never by report forwarding.
class InputReconnectPolicy {
public:
    using Clock=std::chrono::steady_clock;
    static bool Eligible(const std::string& address,const std::string& outputHost,
                         const std::string& icon,bool paired,bool trusted,bool blocked){
        return !address.empty()&&address!=outputHost&&paired&&trusted&&!blocked&&
               (icon=="input-keyboard"||icon=="input-mouse");
    }
    bool Begin(const std::string& address,bool eligible,bool connected,Clock::time_point now){
        if(!eligible||connected){attempts.erase(address);return false;}
        if(paused.count(address))return false;
        auto& attempt=attempts[address];
        if(attempt.pending||now<attempt.next)return false;
        attempt.pending=true;
        attempt.next=now+std::chrono::seconds(std::min(60u,5u<<std::min(attempt.failures,4u)));
        ++attempt.failures;
        return true;
    }
    void Complete(const std::string& address){auto it=attempts.find(address);if(it!=attempts.end())it->second.pending=false;}
    void Pause(const std::string& address){paused.insert(address);}
    void Resume(const std::string& address){
        paused.erase(address);
        auto& attempt=attempts[address];attempt.failures=0;attempt.next={};
        // An earlier async call still owns its completion; do not overlap it.
    }
private:
    struct Attempt {bool pending=false;unsigned failures=0;Clock::time_point next{};};
    std::map<std::string,Attempt> attempts;
    std::set<std::string> paused;
};
