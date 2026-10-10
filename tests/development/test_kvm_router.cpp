// Test the actual router with anonymous USB pipes and a stub Bluetooth output.
#include "../../raspikey/KvmRouter.cpp"
#include <cassert>
#include <iostream>

namespace {unsigned wakeRequests=0,connectionRequests=0,bluetoothReports=0;bool wakeAccepted=true;}
void StartKvmBluetooth(){}
void StopKvmBluetooth(){}
bool SendKvmBluetooth(uint8_t,const uint8_t*,size_t){++bluetoothReports;return true;}
bool WakeKvmBluetoothHost(){++wakeRequests;return wakeAccepted;}
void ForgetKvmBluetoothHost(const char*){}
bool RequestKvmBluetoothConnection(){++connectionRequests;return true;}
void PauseKvmBluetoothAdvertising(){}

int main(){
    int keyboardPipe[2],mousePipe[2];assert(pipe(keyboardPipe)==0&&pipe(mousePipe)==0);
    usb[0]=keyboardPipe[1];usb[1]=mousePipe[1];enabled=true;registered=true;ready=true;
    KvmRelease(false);KvmRelease(true);assert(bluetoothReports==0&&wakeRequests==0);
    Switch(true);assert(bluetooth&&wakeRequests==1);
    input.keyboard={1,0,4};input.buttons=1;
    KvmBluetoothSleeping(true);assert(!bluetooth&&sleeping&&input.quarantine&&ready);
    KvmBluetoothSleeping(true);assert(!bluetooth&&wakeRequests==1);
    const uint8_t held[9]={1,1,0,4};KvmForward(false,held,9);assert(!bluetooth&&wakeRequests==1);
    const uint8_t mouseRelease[9]={1},keyRelease[9]={1};KvmForward(true,mouseRelease,9);KvmForward(false,keyRelease,9);
    assert(!input.quarantine);
    const uint8_t chord[9]={1,5,0,69};KvmForward(false,chord,9);assert(bluetooth&&wakeRequests==2);
    KvmBluetoothSleeping(false);assert(bluetooth&&!sleeping);
    KvmForward(false,keyRelease,9);KvmForward(false,chord,9);assert(!bluetooth&&wakeRequests==2);
    KvmForward(false,keyRelease,9);
    KvmBluetoothSleeping(true);Switch(true);assert(bluetooth&&wakeRequests==3);
    KvmBluetoothSleeping(false);Switch(true);assert(wakeRequests==3);
    Switch(false);wakeAccepted=false;Switch(true);assert(!bluetooth&&!error.empty()&&wakeRequests==4);
    wakeAccepted=true;ready=false;Switch(true);assert(!bluetooth&&wakeRequests==4);
    KvmBluetoothStatus(false,false,"");assert(!sleeping);
    hostAddress="FC:B2:14:77:1B:F3";Switch(true);assert(connecting&&!bluetooth);
    Switch(false);assert(!connecting&&!bluetooth); // cancelling never wakes later
    auto before=wakeRequests;KvmBluetoothStatus(true,true,"");assert(!bluetooth&&wakeRequests==before);
    ready=false;Switch(true);assert(connecting&&!bluetooth);
    KvmBluetoothStatus(true,true,"");assert(!connecting&&bluetooth&&wakeRequests==before+1);
    Switch(false);ready=false;Switch(true);connectDeadline=Clock::now()-std::chrono::seconds(1);
    KvmBluetoothStatus(true,true,"");assert(!connecting&&!bluetooth&&!error.empty()&&wakeRequests==before+1);
    ready=false;Switch(true);assert(connecting);
    KvmForward(false,keyRelease,9);KvmForward(false,chord,9);assert(!connecting&&!bluetooth);
    KvmBluetoothStatus(true,true,"");assert(!bluetooth&&wakeRequests==before+1);
    assert(nlohmann::json::parse(GetKvmStatus()).at("status").at("sleeping")==false);
    for(int fd:{keyboardPipe[0],keyboardPipe[1],mousePipe[0],mousePipe[1]})close(fd);
    usb[0]=usb[1]=-1;
    std::cout<<"KVM automatic sleep fallback, held-input release, shortcut wake and USB recovery passed\n";
}
