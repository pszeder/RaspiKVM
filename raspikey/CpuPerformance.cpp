#include "CpuPerformance.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>

namespace {
constexpr const char* governor="/sys/devices/system/cpu/cpufreq/policy0/scaling_governor";
constexpr const char* available="/sys/devices/system/cpu/cpufreq/policy0/scaling_available_governors";
constexpr const char* savedPath="/run/raspikey-cpu-previous";
std::string Read(const char* path){std::ifstream file(path);std::string value;file>>value;return value;}
bool Supported(const std::string& value){std::ifstream file(available);std::string word;while(file>>word)if(word==value)return true;return false;}
bool Set(const std::string& value){if(!Supported(value))return false;std::ofstream file(governor);file<<value<<'\n';file.close();return file.good()&&Read(governor)==value;}
std::string Saved() {
    int fd=open(savedPath,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return {};
    struct stat info{};char data[64]{};ssize_t count=-1;
    if(fstat(fd,&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==0&&(info.st_mode&077)==0&&info.st_size>0&&info.st_size<64)count=read(fd,data,sizeof(data)-1);
    close(fd);if(count<=0)return {};
    std::string value(data,size_t(count));return Supported(value)?value:std::string{};
}
bool Save(const std::string& value) {
    int fd=open(savedPath,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return false;
    bool ok=write(fd,value.data(),value.size())==ssize_t(value.size());close(fd);
    if(!ok)unlink(savedPath);
    return ok;
}
}
struct CpuPerformance::Impl {
    bool requested=false,applied=false;
    std::string previous;
    Impl() {
        requested=access("/var/cache/raspikey-build/build/fixed-cpu",F_OK)==0;
        previous=Saved();
        if(requested&&Supported("performance")) {
            if(previous.empty()) {
                std::string current=Read(governor);
                if(Supported(current)&&Save(current))previous=current;
            }
            if(!previous.empty())applied=Set("performance");
        } else Restore();
        Publish();
    }
    ~Impl(){Restore();Publish();}
    void Restore() {
        if(previous.empty())return;
        // Do not replace a governor selected independently while we ran.
        if(Read(governor)!="performance"||Set(previous)){unlink(savedPath);previous.clear();applied=false;}
    }
    void Publish() {
        std::ostringstream text;text<<"{\"requested\":"<<(requested?"true":"false")<<",\"applied\":"<<(applied?"true":"false")
            <<",\"governor\":\""<<Read(governor)<<"\",\"maxFrequencyKhz\":\""<<Read("/sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq")<<"\"}\n";
        int fd=open("/run/raspikey-cpu-performance.json.next",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);if(fd<0)return;
        std::string data=text.str();bool ok=write(fd,data.data(),data.size())==ssize_t(data.size());
        if(ok)ok=fchmod(fd,0644)==0;
        close(fd);if(ok)rename("/run/raspikey-cpu-performance.json.next","/run/raspikey-cpu-performance.json");
    }
};
CpuPerformance::CpuPerformance():impl(new Impl){}
CpuPerformance::~CpuPerformance()=default;
