// Exercise the real transport's readiness/report gates without radio or input.
#include "../../raspikey/KvmBluetooth.cpp"
#include <cassert>
#include <iostream>

namespace {bool statusRegistered=false,statusReady=false;}
bool KvmBluetoothEnabled(){return true;}
bool KvmPairingOpen(){return false;}
bool KvmAcceptHost(const std::string&){return true;}
std::string KvmHostAddress(){return {};}
void KvmHostSeen(const std::string&,const std::string&,bool,bool){}
void KvmHostLed(bool,uint8_t){}
void KvmBluetoothStatus(bool active,bool ready,const std::string&){statusRegistered=active;statusReady=ready;}

int main(){
    Server server;
    auto keyboard=std::unique_ptr<Node>(new Node);keyboard->report=1;keyboard->notify=true;
    auto mouse=std::unique_ptr<Node>(new Node);mouse->report=2;mouse->notify=true;
    server.nodes.push_back(std::move(keyboard));server.nodes.push_back(std::move(mouse));
    server.application=true;server.connected=true;server.paired=true;
    server.Status();assert(statusRegistered&&statusReady);
    // Reproduce Mac sleep/wake with no Exit Suspend and no aggregate
    // Connected=false edge. Existing bonded CCC subscriptions remain enabled.
    server.suspended=true;server.Status();assert(statusReady);
    assert(server.ReportEnabled(*server.nodes[0]));assert(server.ReportEnabled(*server.nodes[1]));
    server.nodes[1]->notify=false;server.Status();assert(!statusReady);
    assert(!server.ReportEnabled(*server.nodes[1]));server.nodes[1]->notify=true;
    server.connected=false;server.Status();assert(!statusReady);
    assert(!server.ReportEnabled(*server.nodes[0]));
    server.connected=true;server.paired=false;server.Status();assert(!statusReady);
    assert(!server.ReportEnabled(*server.nodes[0]));server.paired=true;
    server.protocol=0;server.Status();assert(!statusReady);
    assert(!server.ReportEnabled(*server.nodes[0]));
    for(auto& node:server.nodes)node->boot=true;
    server.Status();assert(statusReady);assert(server.ReportEnabled(*server.nodes[0]));
    server.application=false;server.Status();assert(!statusRegistered&&!statusReady);
    assert(!server.ReportEnabled(*server.nodes[0]));
    server.LinkClosed();assert(!server.connected&&!server.suspended&&server.protocol==1);
    std::cout<<"Bluetooth HID suspend/wake, subscription, protocol and disconnect regression tests passed\n";
}
