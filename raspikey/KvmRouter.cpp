#include "KvmRouter.h"
#include "KvmBluetooth.h"
#include "KvmInputState.h"
#include "KvmPower.h"
#include "Globals.h"
#include "json.hpp"
#include <mutex>
#include <chrono>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <cctype>

namespace {
using Clock=std::chrono::steady_clock;
std::mutex mutex;
KvmInputState input;
int usb[2]={-1,-1};
bool enabled=false,bluetooth=false,registered=false,ready=false;
bool sleeping=false;
bool connecting=false;
Clock::time_point connectDeadline{};
bool reverseBluetoothScroll=false;
uint8_t modifiers=5,key=69;
std::string hostAddress,hostName,pendingAddress,error;
Clock::time_point pairing{};
uint64_t switches=0;
int ledWake=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
uint8_t leds[2]={};bool pendingLed=false;
void Led(){pendingLed=true;uint64_t one=1;if(ledWake>=0)write(ledWake,&one,sizeof(one));}
const std::string path=std::string(DATA_DIR)+"/kvm.json";
std::string Address(std::string value) {
    if(value.empty())return value;
    if(value.size()!=17)throw std::invalid_argument("Invalid Bluetooth host address.");
    for(size_t i=0;i<17;++i){if(i%3==2){if(value[i]!=':')throw std::invalid_argument("Invalid Bluetooth host address.");}else{if(!isxdigit(static_cast<unsigned char>(value[i])))throw std::invalid_argument("Invalid Bluetooth host address.");value[i]=char(toupper(static_cast<unsigned char>(value[i])));}}
    return value;
}
bool Pairing(){return enabled&&Clock::now()<pairing;}
nlohmann::json Settings(){return {{"enabled",enabled},{"shortcut",{{"modifiers",modifiers},{"key",key}}},{"hostAddress",hostAddress},{"reverseBluetoothScroll",reverseBluetoothScroll}};}
void Save(){auto data=Settings().dump();auto temp=path+".next";int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)throw std::runtime_error("Could not save host settings.");bool ok=write(fd,data.data(),data.size())==ssize_t(data.size())&&fsync(fd)==0;close(fd);if(!ok||rename(temp.c_str(),path.c_str())){unlink(temp.c_str());throw std::runtime_error("Could not save host settings.");}}
bool Usb(unsigned source,const uint8_t* report,size_t size){
    if(usb[source]<0)return false;
    auto deadline=Clock::now()+std::chrono::milliseconds(100);
    do {ssize_t n=write(usb[source],report,size);if(n==ssize_t(size))return true;if(n>=0||(errno!=EINTR&&errno!=EAGAIN))return false;pollfd wait{usb[source],POLLOUT,0};poll(&wait,1,5);}while(Clock::now()<deadline);
    return false;
}
void ReleaseUsb(){uint8_t keyboard[9]={1},mouse[9]={1},aux[9]={2},consumer[3]={3},media[2]={2};Usb(0,keyboard,9);Usb(0,media,2);Usb(1,mouse,9);Usb(1,aux,9);Usb(1,consumer,3);}
void ReleaseBluetooth(){const uint8_t zero[8]={};for(uint8_t id:{1,2,3})SendKvmBluetooth(id,zero,id==3?2:8);}
void Switch(bool target){
    if(!target){connecting=false;PauseKvmBluetoothAdvertising();}
    // Always clear the old host, including when an unavailable-host shortcut
    // is consumed. Held modifiers must not remain down on the USB computer.
    if(bluetooth)ReleaseBluetooth();else ReleaseUsb();
    input.Switched();
    if(target&&(!enabled||!ready)){
        if(enabled&&!hostAddress.empty()&&RequestKvmBluetoothConnection()){
            connecting=true;connectDeadline=Clock::now()+std::chrono::seconds(15);error="Connecting to Bluetooth host. Input remains on USB.";
        }else error="Bluetooth host is not ready. Input remains on USB.";
        return;
    }
    connecting=false;
    bool selectingBluetooth=target&&!bluetooth;
    if(bluetooth!=target){bluetooth=target;++switches;Led();}error.clear();
    if(bluetooth)ReleaseBluetooth();else ReleaseUsb();
    // Always request wake on selection: a host may wake without Exit Suspend,
    // or its last sleep hint may have been lost during application restart.
    if(selectingBluetooth){RequestKvmBluetoothConnection();if(!WakeKvmBluetoothHost()){Switch(false);error="Could not queue Bluetooth wake. Input returned to USB.";}}
}
uint16_t Media(uint8_t bits){const uint16_t usages[]={0xb5,0xb6,0xb7,0xcd,0xe2,0xe9,0xea};for(unsigned i=0;i<7;++i)if(bits&(1<<i))return usages[i];return 0;}
void Validate(const nlohmann::json& config,bool& nextEnabled,uint8_t& nextMods,uint8_t& nextKey,bool& nextReverse){
    if(!config.is_object()||!config.at("enabled").is_boolean())throw std::invalid_argument("Enable Bluetooth must be true or false.");
    auto& shortcut=config.at("shortcut");auto& m=shortcut.at("modifiers");auto& k=shortcut.at("key");
    if(!m.is_number_integer()||!k.is_number_integer())throw std::invalid_argument("Shortcut modifiers and key must be integers.");
    auto mod=m.get<int64_t>(),usage=k.get<int64_t>();
    if(mod<1||mod>15||!(mod&13)||usage<4||usage>101)throw std::invalid_argument("Choose Ctrl, Alt or Win/Cmd and a valid key.");
    nextEnabled=config.at("enabled").get<bool>();nextMods=uint8_t(mod);nextKey=uint8_t(usage);
    if(config.count("reverseBluetoothScroll")&&!config.at("reverseBluetoothScroll").is_boolean())throw std::invalid_argument("Reverse Bluetooth scrolling must be true or false.");
    nextReverse=config.value("reverseBluetoothScroll",false);
}
}
void StartKvm(){
    {std::lock_guard<std::mutex> lock(mutex);std::ifstream file(path);if(file.good())try{nlohmann::json config;file>>config;Validate(config,enabled,modifiers,key,reverseBluetoothScroll);hostAddress=Address(config.value("hostAddress",std::string()));}catch(const std::exception&){enabled=false;error="Saved host settings are invalid. Save them again.";}
    for(unsigned i=0;i<2;++i)usb[i]=open(i?"/dev/hidg1":"/dev/hidg0",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
    ReleaseUsb();}
    StartKvmBluetooth();
}
void StopKvm(){
    {std::lock_guard<std::mutex> lock(mutex);ReleaseUsb();if(bluetooth)ReleaseBluetooth();bluetooth=false;connecting=false;}
    StopKvmBluetooth();
    std::lock_guard<std::mutex> lock(mutex);for(auto& fd:usb){if(fd>=0)close(fd);fd=-1;}
}
int KvmForward(bool mouse,const uint8_t* report,size_t size){
    std::lock_guard<std::mutex> lock(mutex);
    if(!report||!size)return 0;
    size=KvmWireSize(mouse,report,size);
    uint8_t id=0;const uint8_t* payload=report+1;size_t length=size-1;
    std::array<uint8_t,8> keyboard{},pointer{};uint8_t media[2]={};
    if(!mouse&&report[0]==1&&size==9){std::copy_n(payload,8,input.keyboard.begin());id=1;}
    else if(!mouse&&report[0]==2&&size==2){input.consumer=Media(report[1]);id=3;}
    else if(mouse&&report[0]==1&&size==9){input.buttons=uint16_t(report[1])|(uint16_t(report[2])<<8);id=2;}
    else if(mouse&&report[0]==2&&size==9){std::copy_n(payload,8,input.auxiliary.begin());id=1;}
    else if(mouse&&report[0]==3&&size==3){input.auxiliaryConsumer=uint16_t(report[1])|(uint16_t(report[2])<<8);id=3;}
    if(!mouse&&id==1){bool trigger=input.Trigger(modifiers,key);if(enabled&&trigger){Switch(!bluetooth&&!connecting);return int(size);}}
    if(input.Suppressed())return int(size);
    if(!bluetooth)return Usb(mouse?1:0,report,size)?int(size):-1;
    if(!id)return int(size);
    if(id==2){pointer=KvmMousePayload(payload,reverseBluetoothScroll);payload=pointer.data();length=pointer.size();}
    if(id==1){keyboard=input.MergedKeyboard();payload=keyboard.data();length=keyboard.size();}
    if(id==3){uint16_t usage=input.consumer?input.consumer:input.auxiliaryConsumer;media[0]=uint8_t(usage);media[1]=uint8_t(usage>>8);payload=media;length=2;}
    if(!SendKvmBluetooth(id,payload,length)){ready=false;Switch(false);error="Bluetooth output stopped. Input returned to USB.";}
    return int(size);
}
void KvmRelease(bool mouse){std::lock_guard<std::mutex> lock(mutex);if(mouse){input.auxiliary={};input.buttons=0;input.auxiliaryConsumer=0;}else{input.keyboard={};input.consumer=0;input.chordHeld=false;}ReleaseUsb();if(bluetooth)ReleaseBluetooth();input.Switched();}
std::string GetKvmStatus(){std::lock_guard<std::mutex> lock(mutex);return nlohmann::json{{"settings",Settings()},{"status",{{"activeHost",bluetooth?"bluetooth":"usb"},{"registered",registered},{"ready",ready},{"sleeping",sleeping},{"connecting",connecting},{"pairing",Pairing()},{"pairingSeconds",Pairing()?std::chrono::duration_cast<std::chrono::seconds>(pairing-Clock::now()).count():0},{"hostName",hostName},{"error",error},{"switches",switches}}}}.dump();}
std::string UpdateKvm(const std::string& body){
    if(body.size()>2048)throw std::invalid_argument("Host settings are too large.");
    try{auto request=nlohmann::json::parse(body);auto action=request.at("action").get<std::string>();
    {std::lock_guard<std::mutex> lock(mutex);
    if(action=="settings"){bool e,r;uint8_t m,k;Validate(request.at("settings"),e,m,k,r);auto oldEnabled=enabled,oldReverse=reverseBluetoothScroll;auto oldModifiers=modifiers,oldKey=key;enabled=e;modifiers=m;key=k;reverseBluetoothScroll=r;try{Save();}catch(...){enabled=oldEnabled;modifiers=oldModifiers;key=oldKey;reverseBluetoothScroll=oldReverse;throw;}if(!enabled){pairing={};Switch(false);}error.clear();}
    else if(action=="switch"){auto target=request.at("host").get<std::string>();if(target!="usb"&&target!="bluetooth")throw std::invalid_argument("Choose USB or Bluetooth.");Switch(target=="bluetooth");}
    else if(action=="pair"){if(!enabled)throw std::invalid_argument("Enable Bluetooth output first.");if(!hostAddress.empty())throw std::invalid_argument("Forget the existing output host before pairing another.");pendingAddress.clear();pairing=Clock::now()+std::chrono::seconds(120);error.clear();}
    else if(action=="forget"){Switch(false);auto previous=hostAddress;hostAddress.clear();try{Save();}catch(...){hostAddress=previous;throw;}if(!previous.empty())ForgetKvmBluetoothHost(previous.c_str());pendingAddress.clear();hostName.clear();ready=false;pairing={};}
    else throw std::invalid_argument("Unknown host action.");}
    }catch(const nlohmann::json::exception&){throw std::invalid_argument("Invalid host settings.");}
    return GetKvmStatus();
}
bool KvmBluetoothEnabled(){std::lock_guard<std::mutex> lock(mutex);return enabled;}
bool KvmPairingOpen(){std::lock_guard<std::mutex> lock(mutex);return Pairing();}
std::string KvmHostAddress(){std::lock_guard<std::mutex> lock(mutex);return hostAddress.empty()?pendingAddress:hostAddress;}
bool KvmAcceptHost(const std::string& raw){auto address=Address(raw);std::lock_guard<std::mutex> lock(mutex);if(!enabled)return false;if(!hostAddress.empty())return address==hostAddress;if(!Pairing())return false;if(!pendingAddress.empty())return address==pendingAddress;pendingAddress=address;return true;}
void KvmHostSeen(const std::string& raw,const std::string& name,bool connected,bool paired){auto address=Address(raw);std::lock_guard<std::mutex> lock(mutex);if(address!=hostAddress&&address!=pendingAddress)return;hostName=name;if(paired&&hostAddress.empty()&&Pairing()){hostAddress=address;try{Save();pairing={};}catch(const std::exception& ex){error=ex.what();hostAddress.clear();}}if(!connected){ready=false;if(bluetooth)Switch(false);}}
void KvmBluetoothStatus(bool active,bool available,const std::string& message){std::lock_guard<std::mutex> lock(mutex);registered=active;ready=available;if(!active)sleeping=false;if(!ready&&bluetooth)Switch(false);bool finishingConnection=connecting;if(connecting){if(Clock::now()>=connectDeadline){connecting=false;PauseKvmBluetoothAdvertising();error="Bluetooth host did not become ready. Input remains on USB.";}else if(ready)Switch(true);}if(!message.empty())error=message;else if(ready&&!connecting&&!finishingConnection)error.clear();}
bool KvmBluetoothSelected(){std::lock_guard<std::mutex> lock(mutex);return enabled&&bluetooth;}
void KvmBluetoothSleeping(bool value){std::lock_guard<std::mutex> lock(mutex);if(sleeping==value)return;sleeping=value;if(sleeping&&bluetooth)Switch(false);}
int KvmLedFd(){return ledWake;}
bool KvmReadLed(uint8_t* report){std::lock_guard<std::mutex> lock(mutex);uint64_t count;while(read(ledWake,&count,sizeof(count))>0){}if(!pendingLed)return false;pendingLed=false;report[0]=1;report[1]=leds[bluetooth?1:0];return true;}
void KvmHostLed(bool host,uint8_t value){std::lock_guard<std::mutex> lock(mutex);leds[host?1:0]=uint8_t(value&31);if(host==bluetooth)Led();}
