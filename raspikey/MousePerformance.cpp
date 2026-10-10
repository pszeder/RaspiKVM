#include "MousePerformance.h"
#include "MouseTiming.h"
#include "MouseLinkProtocol.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <time.h>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
uint64_t Now(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000+t.tv_nsec/1000;}
struct HciAddress {sa_family_t family;uint16_t device,channel;};
struct HciFilter {uint32_t types,events[2];uint16_t opcode;};
struct HciConnection {uint16_t handle;uint8_t address[6],type,out;uint16_t state;uint32_t mode;};
struct ConnectionRequest {uint8_t address[6],type;HciConnection connection;};
static_assert(offsetof(ConnectionRequest,connection)==8&&sizeof(HciConnection)==16,"HCI ioctl ABI");
int Socket(uint16_t device,uint16_t channel) {
    int fd=socket(AF_BLUETOOTH,SOCK_RAW|SOCK_NONBLOCK|SOCK_CLOEXEC,1);
    if(fd<0)return -1;
    HciAddress bindAddress{AF_BLUETOOTH,device,channel};
    if(bind(fd,reinterpret_cast<sockaddr*>(&bindAddress),sizeof(bindAddress))<0){close(fd);return -1;}
    return fd;
}
std::string AddressText(const uint8_t* bytes) {
    char text[18];snprintf(text,sizeof(text),"%02X:%02X:%02X:%02X:%02X:%02X",bytes[5],bytes[4],bytes[3],bytes[2],bytes[1],bytes[0]);return text;
}
}

struct MousePerformance::Impl {
    bool lowLatency=access("/var/cache/raspikey-build/mouse-baseline",F_OK)!=0;
    bool connected=false,niceApplied=false,requested=false,acknowledged=false,confirmed=false;
    int raw=-1,control=-1,controller=-1,status=-1;
    uint16_t handle=0,interval=0,latency=0,timeout=0;
    uint8_t addressType=0;
    std::array<uint8_t,6> address{};
    MouseTiming timing;
    uint64_t lastPublish=0,attached=0,lastRequest=0;
    unsigned attempts=0;
    Impl() {
        pthread_setname_np(pthread_self(),"raspikey-mouse");
        if(lowLatency)niceApplied=setpriority(PRIO_PROCESS,syscall(SYS_gettid),-10)==0;
    }
    ~Impl(){Close();}
    void Close(){if(raw>=0)close(raw);if(control>=0)close(control);raw=control=-1;}
    void Attach(const std::string& text,bool mxMaster3s) {
        Close();connected=true;timing=MouseTiming{};controller=-1;interval=latency=timeout=0;
        requested=acknowledged=confirmed=false;status=-1;addressType=0;attempts=0;attached=Now();lastRequest=0;
        if(!mxMaster3s||!MouseLinkProtocol::Address(text,address))return;
        glob_t paths{};
        if(glob("/sys/class/bluetooth/hci[0-9]*",0,nullptr,&paths)!=0)return;
        for(size_t i=0;i<paths.gl_pathc;++i) {
            const char* leaf=strrchr(paths.gl_pathv[i],'/');char* end=nullptr;
            long index=strtol(leaf+4,&end,10);
            if(*end||index<0||index>=0xffff)continue;
            int candidate=Socket(uint16_t(index),0);if(candidate<0)continue;
            ConnectionRequest query{};std::memcpy(query.address,address.data(),6);query.type=0x80;
            if(ioctl(candidate,_IOR('H',213,int),&query)<0){close(candidate);continue;}
            HciFilter filter{};filter.types=1u<<4;filter.events[1]=1u<<30;
            if(setsockopt(candidate,0,2,&filter,sizeof(filter))<0){close(candidate);continue;}
            raw=candidate;controller=int(index);handle=query.connection.handle;
            // Read only the address type from the paired profile; never expose
            // or modify pairing keys or overwrite BlueZ's persisted settings.
            alignas(uint32_t) uint8_t info[128]{};MouseLinkProtocol::Put16(info,uint16_t(index));
            if(ioctl(raw,_IOR('H',211,int),info)==0) {
                std::ifstream profile("/var/lib/bluetooth/"+AddressText(info+2)+"/"+AddressText(address.data())+"/info");
                std::string line;bool general=false;
                while(std::getline(profile,line)) {
                    if(!line.empty()&&line[0]=='[')general=line=="[General]";
                    if(general&&line=="AddressType=public")addressType=1;
                    if(general&&(line=="AddressType=static"||line=="AddressType=random"))addressType=2;
                }
            }
            break;
        }
        globfree(&paths);
        if(lowLatency&&raw>=0&&addressType) {
            control=Socket(0xffff,3);
            Request();
        }
    }
    void Request() {
        if(control<0||attempts>=2)return;
        auto packet=MouseLinkProtocol::Request(uint16_t(controller),address,addressType);
        if(write(control,packet.data(),packet.size())==ssize_t(packet.size())){requested=true;lastRequest=Now();++attempts;}
    }
    void PollLink() {
        uint8_t packet[512];
        if(raw>=0)for(unsigned i=0;i<16;++i) {
            ssize_t n=read(raw,packet,sizeof(packet));if(n<=0)break;
            MouseLinkProtocol::Update update;
            if(MouseLinkProtocol::ConnectionUpdate(packet,size_t(n),handle,update)) {
                status=update.status;
                if(!update.status){confirmed=true;interval=update.interval;latency=update.latency;timeout=update.timeout;}
            }
        }
        if(control>=0)for(unsigned i=0;i<16;++i) {
            ssize_t n=read(control,packet,sizeof(packet));if(n<9)break;
            uint8_t result=0;
            if(MouseLinkProtocol::CommandResult(packet,size_t(n),uint16_t(controller),result)) {
                acknowledged=result==0;if(result)status=result;
            }
        }
        // One bounded retry after the peripheral's initial negotiation settles.
        // Unsupported/rejected tuning must never interrupt input forwarding.
        if(lowLatency&&requested&&attempts==1&&Now()-attached>=5000000&&
           (!confirmed||interval!=6||latency!=0))Request();
    }
    void Publish(bool force=false) {
        uint64_t now=Now();if(!force&&now-lastPublish<3000000)return;lastPublish=now;
        std::ostringstream text;
        text<<"{\"connected\":"<<(connected?"true":"false")<<",\"lowLatency\":"<<(lowLatency?"true":"false")
            <<",\"niceApplied\":"<<(niceApplied?"true":"false")<<",\"controller\":"<<controller
            <<",\"linkRequested\":"<<(requested?"true":"false")<<",\"linkAcknowledged\":"<<(acknowledged?"true":"false")
            <<",\"linkConfirmed\":"<<(confirmed?"true":"false")<<",\"linkStatus\":"<<status
            <<",\"connectionIntervalUs\":"<<uint64_t(interval)*1250<<",\"peripheralLatency\":"<<latency
            <<",\"motionFrames\":"<<timing.frames<<",\"activeGaps\":"<<timing.gaps
            <<",\"meanActiveGapUs\":"<<(timing.gaps?timing.gapSum/timing.gaps:0)
            <<",\"gapP50UpperUs\":"<<MouseTiming::Percentile(timing.gapHistogram,timing.gaps,50,500)
            <<",\"meanQueueUs\":"<<(timing.frames?timing.queueSum/timing.frames:0)
            <<",\"queueP95UpperUs\":"<<MouseTiming::Percentile(timing.queueHistogram,timing.frames,95,100)
            <<",\"queueOverflowFrames\":"<<timing.queueHistogram.back()
            <<",\"meanReadDelayUs\":"<<(timing.frames?timing.readSum/timing.frames:0)
            <<",\"meanWriteWaitUs\":"<<(timing.frames?timing.writeSum/timing.frames:0)
            <<",\"maxQueueUs\":"<<timing.maxQueue<<",\"maxWriteWaitUs\":"<<timing.maxWrite<<"}\n";
        const char* staging="/run/raspikey-mouse-performance.json.next";
        int fd=open(staging,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);
        if(fd>=0) {
            std::string data=text.str();bool ok=write(fd,data.data(),data.size())==ssize_t(data.size());
            if(ok)ok=fchmod(fd,0644)==0;
            close(fd);
            if(ok)rename(staging,"/run/raspikey-mouse-performance.json");
        }
    }
};

MousePerformance::MousePerformance():impl(new Impl){}
MousePerformance::~MousePerformance()=default;
void MousePerformance::Connect(const std::string& address,bool mxMaster3s){impl->Attach(address,mxMaster3s);impl->Publish(true);}
void MousePerformance::Disconnect(){impl->connected=false;impl->Close();impl->Publish(true);}
void MousePerformance::Frame(uint64_t stamp,uint64_t before,uint64_t after){impl->timing.Frame(stamp,before,after);}
void MousePerformance::Tick(){impl->PollLink();impl->Publish();}
