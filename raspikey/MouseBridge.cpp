#include "MouseBridge.h"
#include "MouseReports.h"
#include "MouseKeys.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <glob.h>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <vector>

namespace {
std::mutex stateMutex;
MouseBridgeStatus state;
constexpr size_t KeyWords=(KEY_MAX+8*sizeof(long))/(8*sizeof(long));
bool Bit(const unsigned long* bits,unsigned bit) {return (bits[bit/(8*sizeof(long))]>>(bit%(8*sizeof(long))))&1;}
struct Device {int fd=-1; bool dropped=false; bool mxMaster3s=false; bool monotonic=false; MouseReports mouse;MouseKeys keys;};
bool Write(int fd,const uint8_t* report,size_t size,const std::atomic<bool>& stopping) {
    while(!stopping) {
        ssize_t written=write(fd,report,size);
        if(written==ssize_t(size))return true;
        if(written<0&&(errno==EAGAIN||errno==EINTR)) {
            pollfd ready{fd,POLLOUT,0};
            int result=poll(&ready,1,50);
            if(result<0&&errno!=EINTR)return false;
            if(ready.revents&(POLLERR|POLLHUP|POLLNVAL))return false;
            continue;
        }
        return false;
    }
    return false;
}
void Release(int fd) {
    // Nonblocking best effort; ExecStopPost also releases all collections.
    const uint8_t mouse[9]={1},keyboard[9]={2},consumer[3]={3};
    for(auto report: {std::make_pair(mouse,sizeof(mouse)),std::make_pair(keyboard,sizeof(keyboard)),std::make_pair(consumer,sizeof(consumer))}) {
        pollfd ready{fd,POLLOUT,0};if(poll(&ready,1,100)>0&&(ready.revents&POLLOUT))write(fd,report.first,report.second);
    }
}
std::vector<Device> Discover(std::string& address,std::string& name) {
    glob_t paths{};std::vector<Device> devices;
    if(glob("/dev/input/event*",0,nullptr,&paths)!=0)return devices;
    // Identify the physical Bluetooth mouse first, then its auxiliary keyboard
    // nodes by the same unique Bluetooth address. Never grab another keyboard.
    for(size_t i=0;i<paths.gl_pathc&&address.empty();++i) {
        int fd=open(paths.gl_pathv[i],O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
        input_id id{};unsigned long keys[KeyWords]={},rel[(REL_MAX+8*sizeof(long))/(8*sizeof(long))]={};char unique[128]={},label[256]={};
        ioctl(fd,EVIOCGID,&id);ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(keys)),keys);ioctl(fd,EVIOCGBIT(EV_REL,sizeof(rel)),rel);
        ioctl(fd,EVIOCGUNIQ(sizeof(unique)),unique);ioctl(fd,EVIOCGNAME(sizeof(label)),label);
        if(id.bustype==BUS_BLUETOOTH&&Bit(keys,BTN_LEFT)&&Bit(rel,REL_X)&&Bit(rel,REL_Y)&&unique[0]) {address=unique;name=label;}
        close(fd);
    }
    if(!address.empty())for(size_t i=0;i<paths.gl_pathc;++i) {
        int fd=open(paths.gl_pathv[i],O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
        input_id id{};char unique[128]={};ioctl(fd,EVIOCGID,&id);ioctl(fd,EVIOCGUNIQ(sizeof(unique)),unique);
        if(id.bustype!=BUS_BLUETOOTH||address!=unique||ioctl(fd,EVIOCGRAB,1)<0){close(fd);continue;}
        Device device;device.fd=fd;device.mxMaster3s=id.vendor==0x046d&&id.product==0xb034;
        int clock=CLOCK_MONOTONIC;device.monotonic=ioctl(fd,EVIOCSCLOCKID,&clock)==0;
        unsigned long held[KeyWords]={};ioctl(fd,EVIOCGKEY(sizeof(held)),held);device.mouse.Resync(held);device.keys.Resync(held);
        if(device.mxMaster3s)device.keys.TaskSwitch(Bit(held,BTN_FORWARD));
        devices.push_back(device);
    }
    globfree(&paths);return devices;
}
}
MouseBridgeStatus GetMouseBridgeStatus() {std::lock_guard<std::mutex> lock(stateMutex);return state;}
void MouseBridge::Start(){worker=std::thread(&MouseBridge::Run,this);}
void MouseBridge::Stop(){stopping=true;if(worker.joinable())worker.join();}
void MouseBridge::Run() {
    while(!stopping) {
        int output=open("/dev/hidg1",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
        {std::lock_guard<std::mutex> lock(stateMutex);state.usbReady=output>=0;state.connected=false;state.address.clear();state.name.clear();}
        if(output<0){std::this_thread::sleep_for(std::chrono::milliseconds(250));continue;}
        std::string address,name;auto devices=Discover(address,name);
        if(devices.empty()){close(output);std::this_thread::sleep_for(std::chrono::milliseconds(250));continue;}
        {std::lock_guard<std::mutex> lock(stateMutex);state.connected=true;state.address=address;state.name=name;}
        bool healthy=true;
        while(!stopping&&healthy) {
            std::vector<pollfd> waiting;for(const auto& device:devices)waiting.push_back({device.fd,POLLIN,0});
            int ready=poll(waiting.data(),waiting.size(),50);
            if(ready<0&&errno!=EINTR)break;
            for(size_t i=0;i<devices.size()&&healthy;++i) {
                if(waiting[i].revents&(POLLERR|POLLHUP|POLLNVAL)){healthy=false;break;}
                if(!(waiting[i].revents&POLLIN))continue;
                input_event events[64];ssize_t bytes=read(devices[i].fd,events,sizeof(events));
                if(bytes<=0){if(bytes==0||(errno!=EAGAIN&&errno!=EINTR))healthy=false;continue;}
                auto& device=devices[i];
                for(size_t e=0;e<size_t(bytes)/sizeof(input_event)&&healthy;++e) {
                    auto& event=events[e];
                    if(event.type==EV_SYN&&event.code==SYN_DROPPED){device.dropped=true;std::lock_guard<std::mutex> lock(stateMutex);++state.droppedFrames;continue;}
                    if(device.dropped) {
                        if(event.type!=EV_SYN||event.code!=SYN_REPORT)continue;
                        unsigned long held[KeyWords]={};if(ioctl(device.fd,EVIOCGKEY(sizeof(held)),held)<0){healthy=false;break;}
                        device.mouse.Resync(held);device.keys.Resync(held);
                        if(device.mxMaster3s)device.keys.TaskSwitch(Bit(held,BTN_FORWARD));
                        device.dropped=false;
                    } else {
                        device.mouse.Event(event.type,event.code,event.value);
                        if(event.type==EV_KEY) {
                            device.keys.Event(event.code,event.value);
                            if(device.mxMaster3s&&event.code==BTN_FORWARD&&event.value!=2)device.keys.TaskSwitch(event.value!=0);
                        }
                    }
                    if(event.type!=EV_SYN||event.code!=SYN_REPORT)continue;
                    std::array<uint8_t,9> mouse;std::array<uint8_t,9> keyboard;std::array<uint8_t,3> consumer;
                    uint64_t count=0;
                    while(device.mouse.Next(mouse)&&healthy){healthy=Write(output,mouse.data(),mouse.size(),stopping);++count;}
                    if(device.keys.Keyboard(keyboard)&&healthy){healthy=Write(output,keyboard.data(),keyboard.size(),stopping);++count;}
                    if(device.keys.ConsumerReport(consumer)&&healthy){healthy=Write(output,consumer.data(),consumer.size(),stopping);++count;}
                    if(count&&healthy) {
                        timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
                        int64_t elapsed=(int64_t(now.tv_sec)-int64_t(event.input_event_sec))*1000000+now.tv_nsec/1000-event.input_event_usec;
                        std::lock_guard<std::mutex> lock(stateMutex);state.reports+=count;
                        if(device.monotonic&&elapsed>=0){state.lastForwardUs=uint64_t(elapsed);state.maxForwardUs=std::max(state.maxForwardUs,state.lastForwardUs);}
                    }
                }
            }
        }
        Release(output);close(output);for(auto& device:devices)close(device.fd);
        {std::lock_guard<std::mutex> lock(stateMutex);state.connected=false;}
    }
}
