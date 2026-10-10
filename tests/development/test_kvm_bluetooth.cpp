// Exercise the real transport's sleep/wake behavior without radio or input.
#include "../../raspikey/KvmBluetooth.cpp"
#include <cassert>
#include <iostream>

namespace {bool statusRegistered=false,statusReady=false,hostSleeping=false;unsigned sleepEvents=0;}
bool KvmBluetoothEnabled(){return true;}
bool KvmPairingOpen(){return false;}
bool KvmAcceptHost(const std::string&){return true;}
std::string KvmHostAddress(){return {};}
void KvmHostSeen(const std::string&,const std::string&,bool,bool){}
void KvmHostLed(bool,uint8_t){}
void KvmBluetoothStatus(bool active,bool ready,const std::string&){statusRegistered=active;statusReady=ready;}
void KvmBluetoothSleeping(bool value){hostSleeping=value;++sleepEvents;}

int main(){
    Server server;
    auto keyboard=std::unique_ptr<Node>(new Node);keyboard->report=1;keyboard->notify=true;
    auto mouse=std::unique_ptr<Node>(new Node);mouse->report=2;mouse->notify=true;
    server.nodes.push_back(std::move(keyboard));server.nodes.push_back(std::move(mouse));
    server.application=true;server.connected=true;server.paired=true;
    server.Status();assert(statusRegistered&&statusReady);
    const uint8_t pending[8]={0,0,1};assert(SendKvmBluetooth(2,pending,8));
    server.SetSuspend(true);assert(queue.empty()&&hostSleeping&&sleepEvents==1);
    server.SetSuspend(true);assert(sleepEvents==1);
    server.Status();assert(statusReady); // sleep keeps the wake transport usable
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
    server.WakeHost();assert(server.suspended); // disabled application cannot wake
    server.LinkClosed();assert(!server.connected&&!server.suspended&&!hostSleeping&&server.protocol==1);
    stopping=true;assert(!WakeKvmBluetoothHost());stopping=false;
    std::cout<<"Bluetooth sleep edges, queued-input suppression and transport guard tests passed\n";
}
