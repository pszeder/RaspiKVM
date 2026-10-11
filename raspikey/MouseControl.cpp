#include "MouseControl.h"
#include "MouseConfiguration.h"
#include "HidppProtocol.h"
#include "Globals.h"
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <glob.h>
#include <mutex>
#include <deque>
#include <map>
#include <fstream>
#include <chrono>
#include <thread>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <linux/input.h>
#include <pthread.h>

namespace {
using Clock=std::chrono::steady_clock;
struct State {
    std::mutex mutex;
    std::map<std::string,MouseConfiguration> configs;
    std::deque<MouseShortcutEvent> events;
    int wake=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    std::string address,error,actualMode;
    bool connected=false,supported=false,applied=false,diverted=false;
    uint64_t revision=0,appliedRevision=0,buttonPresses=0;
    uint8_t wheelIndex=0,controlsIndex=0;
    ~State(){if(wake>=0)close(wake);}
} state;
std::string Address(std::string value) {
    if(value.size()!=17)throw std::invalid_argument("Invalid mouse address.");
    for(size_t i=0;i<value.size();++i) {
        if(i%3==2){if(value[i]!=':')throw std::invalid_argument("Invalid mouse address.");}
        else {if(!isxdigit(static_cast<unsigned char>(value[i])))throw std::invalid_argument("Invalid mouse address.");value[i]=char(toupper(static_cast<unsigned char>(value[i])));}
    }
    return value;
}
std::string ConfigPath(const std::string& address) {std::string name=address;for(auto& c:name)if(c==':')c='-';return std::string(DATA_DIR)+"/mouse-"+name+".json";}
MouseConfiguration& Config(const std::string& address) {
    auto existing=state.configs.find(address);if(existing!=state.configs.end())return existing->second;
    MouseConfiguration config;std::ifstream file(ConfigPath(address));
    if(file.good()){std::string body((std::istreambuf_iterator<char>(file)),{});try{config=ParseMouseConfiguration(body);}catch(const std::exception&){state.error="Saved mouse settings are invalid. Save valid settings to replace them.";}}
    return state.configs.emplace(address,config).first->second;
}
void Queue(MouseShortcutEvent event) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if(state.events.size()>=32){state.events.clear();state.events.push_back({255,0,0,false});}
    state.events.push_back(event);uint64_t one=1;if(state.wake>=0)write(state.wake,&one,sizeof(one));
}
void StatusError(const std::string& text){std::lock_guard<std::mutex> lock(state.mutex);state.error=text;state.applied=false;}
class Connection {
public:
    int fd=-1;
    std::string address;
    uint8_t controls=0,wheel=0,readWheel=0,writeWheel=1;
    bool pressed=false,diverted=false;
    std::array<uint8_t,3> originalWheel{};
    void Notification(const uint8_t* data,size_t count) {
        bool next=false;if(!diverted||!Hidpp::Toggle(data,count,controls,next)||next==pressed)return;
        pressed=next;auto action=GetMouseButtonAction(address,4);
        if(next){std::lock_guard<std::mutex> lock(state.mutex);++state.buttonPresses;}
        // Release is always queued, even if the shortcut changed while held.
        if(!next||action.action==1)Queue({4,action.modifiers,action.key,next});
    }
    std::vector<uint8_t> Call(uint8_t feature,uint8_t function,std::vector<uint8_t> parameters={}) {
        auto request=Hidpp::Request(feature,function,parameters);
        ssize_t written=write(fd,request.data(),request.size());
        if(written!=ssize_t(request.size()))throw std::runtime_error("Mouse command could not be sent. Reconnect the mouse and retry.");
        auto deadline=Clock::now()+std::chrono::milliseconds(700);
        while(Clock::now()<deadline) {
            pollfd ready{fd,POLLIN,0};int result=poll(&ready,1,25);
            if(result<0&&errno!=EINTR)break;
            if(ready.revents&(POLLERR|POLLHUP|POLLNVAL))break;
            if(!(ready.revents&POLLIN))continue;
            uint8_t data[64];ssize_t count=read(fd,data,sizeof(data));if(count<=0)continue;
            Notification(data,size_t(count));
            if(Hidpp::Reply(data,size_t(count),feature,function))return {data+4,data+count};
            if(Hidpp::Error(data,size_t(count),feature,function))throw std::runtime_error("Mouse rejected HID++ command (error "+std::to_string(data[5])+").");
        }
        throw std::runtime_error("Mouse did not answer. Wake it or select its RaspiKey slot, then retry.");
    }
    uint8_t Feature(uint16_t id){auto reply=Call(0,0,{uint8_t(id>>8),uint8_t(id),0});return reply.at(0);}
    void Identify() {
        auto version=Call(0,1,{0,0,0x5a});if(version.size()<3||version[0]<2||version[2]!=0x5a)throw std::runtime_error("HID++ 2.0 is unavailable.");
        wheel=Feature(0x2111);if(wheel){readWheel=1;writeWheel=2;}else wheel=Feature(0x2110);
        controls=Feature(0x1b04);
        if(!wheel||!controls)throw std::runtime_error("This mouse does not expose supported wheel/button controls.");
        auto count=Call(controls,0);bool found=false;
        for(unsigned i=0;i<count.at(0)&&i<64;++i) {
            auto info=Call(controls,1,{uint8_t(i)});
            if(info.size()>=5&&info[0]==0&&info[1]==0xc4&&(info[4]&0x20))found=true;
        }
        if(!found)throw std::runtime_error("Scroll-toggle button diversion is not supported.");
        auto current=Call(wheel,readWheel);if(current.size()<3)throw std::runtime_error("Incomplete wheel state.");
        std::copy_n(current.begin(),3,originalWheel.begin());
        std::lock_guard<std::mutex> lock(state.mutex);state.supported=true;state.wheelIndex=wheel;state.controlsIndex=controls;
    }
    void Apply(const MouseConfiguration& config) {
        // Temporary diversion only. Do not change persistent mappings, raw XY,
        // wheel resolution, host selection, DPI, or any unrelated reporting bit.
        bool wanted=config.buttons[4].action!=0;
        auto result=Call(controls,3,{0,0xc4,uint8_t(wanted?3:2),0,0});
        auto reporting=Call(controls,2,{0,0xc4});
        if(reporting.size()<3||reporting[0]!=0||reporting[1]!=0xc4||bool(reporting[2]&1)!=wanted)throw std::runtime_error("Scroll-toggle diversion could not be verified.");
        diverted=wanted;if(!wanted&&pressed){pressed=false;Queue({4,0,0,false});}
        uint8_t mode=config.scrollMode=="free"?1:2;
        uint8_t speed=config.scrollMode=="smart"?uint8_t(config.smartShift):255;
        Call(wheel,writeWheel,{mode,speed,0}); // zero leaves ratchet torque unchanged
        auto actual=Call(wheel,readWheel);
        if(actual.size()<2||actual[0]!=mode||(mode==2&&actual[1]!=speed))throw std::runtime_error("Scroll mode could not be verified.");
        std::lock_guard<std::mutex> lock(state.mutex);state.actualMode=config.scrollMode;state.diverted=diverted;
    }
    void Cleanup() {
        if(pressed){Queue({4,0,0,false});pressed=false;}
        if(fd>=0){if(diverted)try{Call(controls,3,{0,0xc4,2,0,0});}catch(const std::exception&){}close(fd);fd=-1;}
    }
    ~Connection(){Cleanup();}
};
bool Open(Connection& connection) {
    glob_t paths{};if(glob("/dev/hidraw*",0,nullptr,&paths)!=0)return false;
    for(size_t i=0;i<paths.gl_pathc;++i) {
        int fd=open(paths.gl_pathv[i],O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
        hidraw_devinfo info{};if(ioctl(fd,HIDIOCGRAWINFO,&info)!=0||info.bustype!=BUS_BLUETOOTH||uint16_t(info.vendor)!=0x046d||uint16_t(info.product)!=0xb034){close(fd);continue;}
        // HID_UNIQ is verified from sysfs, not a Bluetooth adapter address.
        std::string node=paths.gl_pathv[i];node=node.substr(node.find_last_of('/')+1);
        std::ifstream file("/sys/class/hidraw/"+node+"/device/uevent");std::string line,address;
        while(std::getline(file,line))if(line.compare(0,9,"HID_UNIQ=")==0)address=line.substr(9);
        try{connection.address=Address(address);}catch(const std::exception&){close(fd);continue;}
        connection.fd=fd;break;
    }
    globfree(&paths);return connection.fd>=0;
}
}
int MouseControlEventFd(){return state.wake;}
bool TakeMouseShortcutEvent(MouseShortcutEvent& event) {
    std::lock_guard<std::mutex> lock(state.mutex);if(state.events.empty())return false;
    event=state.events.front();state.events.pop_front();return true;
}
MouseButtonAction GetMouseButtonAction(const std::string& address,unsigned button) {
    if(button>=5)return {};
    std::lock_guard<std::mutex> lock(state.mutex);return Config(Address(address)).buttons[button];
}
std::string GetMouseControlSettings(const std::string& rawAddress) {
    auto address=Address(rawAddress);std::lock_guard<std::mutex> lock(state.mutex);
    bool current=state.address==address;
    return nlohmann::json{{"settings",MouseConfigurationJson(Config(address))},
        {"status",{{"connected",current&&state.connected},{"supported",current&&state.supported},
        {"applied",current&&state.applied},{"diverted",current&&state.diverted},{"actualScrollMode",current?state.actualMode:""},
        {"error",current?state.error:""},{"buttonPresses",current?state.buttonPresses:0},
        {"wheelFeatureIndex",current?state.wheelIndex:0},{"controlsFeatureIndex",current?state.controlsIndex:0}}}}.dump();
}
std::string SetMouseControlSettings(const std::string& rawAddress,const std::string& body) {
    if(body.size()>4096)throw std::invalid_argument("Mouse settings are too large.");
    auto address=Address(rawAddress);auto config=ParseMouseConfiguration(body);
    {std::lock_guard<std::mutex> lock(state.mutex);
        auto path=ConfigPath(address),staging=path+".next";auto data=MouseConfigurationJson(config).dump();
        int fd=open(staging.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);
        if(fd<0)throw std::runtime_error("Could not save mouse settings.");
        bool saved=write(fd,data.data(),data.size())==ssize_t(data.size())&&fsync(fd)==0;close(fd);
        if(!saved||rename(staging.c_str(),path.c_str())!=0){unlink(staging.c_str());throw std::runtime_error("Could not save mouse settings.");}
        state.configs[address]=config;++state.revision;if(state.address==address){state.applied=false;state.error.clear();}
    }
    return GetMouseControlSettings(address);
}
void RunMouseControl(const std::atomic<bool>* stopping) {
    pthread_setname_np(pthread_self(),"raspikey-mxcfg");
    while(!*stopping) {
        Connection connection;
        if(!Open(connection)){std::this_thread::sleep_for(std::chrono::milliseconds(250));continue;}
        {std::lock_guard<std::mutex> lock(state.mutex);state.address=connection.address;state.connected=true;state.supported=false;state.applied=false;state.diverted=false;state.error.clear();state.actualMode.clear();}
        bool identified=false;uint64_t appliedRevision=~uint64_t(0);auto retry=Clock::now();
        while(!*stopping) {
            if(Clock::now()>=retry) {
                MouseConfiguration config;uint64_t revision;
                {std::lock_guard<std::mutex> lock(state.mutex);config=Config(connection.address);revision=state.revision;}
                if(!identified||revision!=appliedRevision)try {
                    if(!identified){connection.Identify();identified=true;}
                    connection.Apply(config);appliedRevision=revision;
                    std::lock_guard<std::mutex> lock(state.mutex);state.applied=state.revision==revision;state.error.clear();state.appliedRevision=revision;
                }catch(const std::exception& error){StatusError(error.what());}
                retry=Clock::now()+std::chrono::seconds(2);
            }
            pollfd ready{connection.fd,POLLIN,0};int result=poll(&ready,1,50);
            if(result<0&&errno!=EINTR)break;
            if(ready.revents&(POLLERR|POLLHUP|POLLNVAL))break;
            if(ready.revents&POLLIN){uint8_t data[64];ssize_t count=read(connection.fd,data,sizeof(data));if(count==0)break;if(count>0)connection.Notification(data,size_t(count));}
        }
        connection.Cleanup();
        {std::lock_guard<std::mutex> lock(state.mutex);state.connected=false;state.applied=false;state.diverted=false;}
    }
}
