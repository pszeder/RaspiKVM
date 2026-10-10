#include "KeyboardPerformance.h"
#include "MouseTiming.h"
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sstream>
#include <cerrno>

namespace {
uint64_t Now(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000+t.tv_nsec/1000;}
}
struct KeyboardPerformance::Impl {
    int event=-1,originalFlags=-1,originalNice=0;
    bool baseline=false,niceApplied=false,connected=true;
    uint64_t published=0,overruns=0;
    MouseTiming bridge,evdev;
    explicit Impl(int fd) {
        pthread_setname_np(pthread_self(),"raspikey-kbd");
        baseline=access("/var/cache/raspikey-build/build/keyboard-baseline",F_OK)==0;
        errno=0;originalNice=getpriority(PRIO_PROCESS,syscall(SYS_gettid));
        if(errno)originalNice=0;
        if(!baseline)niceApplied=setpriority(PRIO_PROCESS,syscall(SYS_gettid),-10)==0;
        originalFlags=fcntl(fd,F_GETFL);
        int clock=CLOCK_MONOTONIC;
        if(originalFlags>=0&&ioctl(fd,EVIOCSCLOCKID,&clock)==0&&
           fcntl(fd,F_SETFL,originalFlags|O_NONBLOCK)==0)event=fd;
        Publish(true);
    }
    ~Impl(){connected=false;Publish(true);if(event>=0)fcntl(event,F_SETFL,originalFlags);if(niceApplied)setpriority(PRIO_PROCESS,syscall(SYS_gettid),originalNice);}
    void Drain() {
        if(event<0)return;
        // evdev is observed only for timestamps. No key code/value is retained
        // or used to alter the existing raw-report forwarding behavior.
        input_event events[64];
        for(unsigned batch=0;batch<16;++batch) {
            ssize_t count=read(event,events,sizeof(events));if(count<=0)break;
            uint64_t now=Now();
            for(size_t i=0;i<size_t(count)/sizeof(input_event);++i) {
                if(events[i].type==EV_SYN&&events[i].code==SYN_DROPPED)++overruns;
                if(events[i].type==EV_SYN&&events[i].code==SYN_REPORT) {
                    uint64_t stamp=uint64_t(events[i].input_event_sec)*1000000+events[i].input_event_usec;
                    evdev.Frame(stamp,now,now);
                }
            }
        }
    }
    void Publish(bool force=false) {
        uint64_t now=Now();if(!force&&now-published<3000000)return;published=now;
        std::ostringstream text;
        text<<"{\"connected\":"<<(connected?"true":"false")<<",\"baseline\":"<<(baseline?"true":"false")
            <<",\"niceApplied\":"<<(niceApplied?"true":"false")<<",\"eventClockMonotonic\":"<<(event>=0?"true":"false")
            <<",\"reports\":"<<bridge.frames<<",\"meanReadToQueueUs\":"<<(bridge.frames?bridge.queueSum/bridge.frames:0)
            <<",\"readToQueueP95UpperUs\":"<<MouseTiming::Percentile(bridge.queueHistogram,bridge.frames,95,100)
            <<",\"meanFilterUs\":"<<(bridge.frames?bridge.readSum/bridge.frames:0)
            <<",\"meanWriteUs\":"<<(bridge.frames?bridge.writeSum/bridge.frames:0)
            <<",\"maxReadToQueueUs\":"<<bridge.maxQueue<<",\"queueOverflowReports\":"<<bridge.queueHistogram.back()
            <<",\"evdevFrames\":"<<evdev.frames<<",\"meanEvdevDelayUs\":"<<(evdev.frames?evdev.queueSum/evdev.frames:0)
            <<",\"evdevDelayP95UpperUs\":"<<MouseTiming::Percentile(evdev.queueHistogram,evdev.frames,95,100)
            <<",\"maxEvdevDelayUs\":"<<evdev.maxQueue<<",\"evdevOverruns\":"<<overruns<<"}\n";
        int fd=open("/run/raspikey-keyboard-performance.json.next",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);
        if(fd<0)return;
        std::string data=text.str();bool ok=write(fd,data.data(),data.size())==ssize_t(data.size());
        if(ok)ok=fchmod(fd,0644)==0;
        close(fd);if(ok)rename("/run/raspikey-keyboard-performance.json.next","/run/raspikey-keyboard-performance.json");
    }
};
KeyboardPerformance::KeyboardPerformance(int fd):impl(new Impl(fd)){}
KeyboardPerformance::~KeyboardPerformance()=default;
int KeyboardPerformance::EventFd()const{return impl->event;}
void KeyboardPerformance::Drain(){impl->Drain();}
void KeyboardPerformance::Report(uint64_t readAt,uint64_t filteredAt,uint64_t queuedAt){impl->bridge.Frame(readAt,filteredAt,queuedAt);}
void KeyboardPerformance::Tick(){impl->Publish();}
