#include "TvControl.h"
#include "Globals.h"
#include "TvWorkerSource.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
extern char** environ;

namespace {
using Json=nlohmann::json;
std::mutex tvMutex;
std::thread tvThread;
std::atomic<bool> stopping{true},activityEnabled{false},movementEnabled{false};
std::atomic<unsigned> activity{0},motion{0},hostState{0},hostGeneration{0};
std::deque<Json> commands;
Json settings={{"enabled",false},{"host",""},{"mac",""},{"wolDestination",""},{"usbInput","HDMI_4"},{"bluetoothInput","HDMI_3"},{"wakeOnInput",true},{"wakeOnMouseMovement",true},{"automaticSwitch",true}};
Json status={{"connected",false},{"paired",false},{"power","unknown"},{"currentInput",""},{"inputs",Json::array()},{"error",""},{"wakeRequests",0}};
const std::string configPath=std::string(DATA_DIR)+"/tv.json";
bool LocalIp(const std::string& value){in_addr addr{};if(inet_pton(AF_INET,value.c_str(),&addr)!=1)return false;auto ip=ntohl(addr.s_addr);return (ip>>24)==10||(ip>>16)==0xc0a8||((ip>>20)==0xac1)||((ip>>16)==0xa9fe);}
Json Validate(const Json& value){
    Json result=settings;
    for(const char* field:{"enabled","wakeOnInput","wakeOnMouseMovement","automaticSwitch"}){if(!value.at(field).is_boolean())throw std::invalid_argument("TV switches must be true or false.");result[field]=value.at(field);}
    for(const char* field:{"host","mac","wolDestination","usbInput","bluetoothInput"}){if(!value.at(field).is_string())throw std::invalid_argument("TV addresses and input names must be text.");auto text=value.at(field).get<std::string>();if(text.size()>64)throw std::invalid_argument("TV setting is too long.");result[field]=text;}
    auto host=result["host"].get<std::string>(),mac=result["mac"].get<std::string>(),dest=result["wolDestination"].get<std::string>();
    if((result["enabled"].get<bool>()||!host.empty())&&!LocalIp(host))throw std::invalid_argument("Enter the TV's local IPv4 address.");
    if(!dest.empty()&&!LocalIp(dest))throw std::invalid_argument("Enter a local wake destination or broadcast address.");
    if(!mac.empty()){if(mac.size()!=17)throw std::invalid_argument("Enter the TV network MAC address (AA:BB:CC:DD:EE:FF).");for(size_t i=0;i<mac.size();++i){if(i%3==2){if(mac[i]!=':')throw std::invalid_argument("Use colons in the TV MAC address.");}else if(!isxdigit(static_cast<unsigned char>(mac[i])))throw std::invalid_argument("Invalid TV MAC address.");}}
    for(const char* field:{"usbInput","bluetoothInput"}){auto input=result[field].get<std::string>();if(input.size()!=6||input.substr(0,5)!="HDMI_"||input[5]<'1'||input[5]>'8')throw std::invalid_argument("Choose an HDMI input from 1 to 8.");}
    return result;
}
void Save(const Json& value){auto data=value.dump(),temp=configPath+".next";int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)throw std::runtime_error("Could not save TV settings.");bool ok=write(fd,data.data(),data.size())==ssize_t(data.size())&&fsync(fd)==0;close(fd);if(!ok||rename(temp.c_str(),configPath.c_str())){unlink(temp.c_str());throw std::runtime_error("Could not save TV settings.");}}
void Queue(Json message){if(commands.size()>=8)throw std::runtime_error("TV controls are busy. Please retry.");commands.push_back(std::move(message));}
void Error(const std::string& message){std::lock_guard<std::mutex> lock(tvMutex);status["connected"]=false;status["error"]=message;}
void Dispatch(){
    int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)){Error("Could not start TV control channel.");return;}
    int source=memfd_create("raspikey-tv-worker",MFD_CLOEXEC);if(source<0){close(pair[0]);close(pair[1]);Error("Could not load TV worker.");return;}
    size_t offset=0;while(offset<TvWorkerSourceSize){auto count=write(source,TvWorkerSource+offset,TvWorkerSourceSize-offset);if(count<=0){close(source);close(pair[0]);close(pair[1]);Error("Could not load TV worker.");return;}offset+=size_t(count);}lseek(source,0,SEEK_SET);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,pair[1],0);posix_spawn_file_actions_adddup2(&actions,pair[1],1);posix_spawn_file_actions_adddup2(&actions,source,3);
    char* args[]={const_cast<char*>("/usr/bin/python3"),const_cast<char*>("-I"),const_cast<char*>("-u"),const_cast<char*>("/proc/self/fd/3"),const_cast<char*>(DATA_DIR),nullptr};
    pid_t child=-1;int spawnError=posix_spawn(&child,args[0],&actions,nullptr,args,environ);posix_spawn_file_actions_destroy(&actions);close(pair[1]);close(source);
    if(spawnError){close(pair[0]);Error("Could not launch Python TV worker.");return;}
    fcntl(pair[0],F_SETFL,fcntl(pair[0],F_GETFL)|O_NONBLOCK);
    std::string sending,receiving;unsigned generation=hostGeneration.load();auto lastActivity=std::chrono::steady_clock::now()-std::chrono::seconds(1);
    {std::lock_guard<std::mutex> lock(tvMutex);sending=Json{{"action","config"},{"settings",settings}}.dump()+"\n";}
    bool failed=false;
    while(!stopping&&!failed){
        if(sending.empty()){
            Json message;{std::lock_guard<std::mutex> lock(tvMutex);if(!commands.empty()){message=commands.front();commands.pop_front();}}
            auto current=hostGeneration.load();if(message.is_null()&&current!=generation){generation=current;unsigned host=hostState.load();message={{"action","host"},{"host",(host&1)?"bluetooth":"usb"},{"explicit",bool(host&2)}};}
            bool input=activity.exchange(0)!=0;unsigned moved=motion.exchange(0);auto now=std::chrono::steady_clock::now();
            if((input||moved>=8)&&now-lastActivity>=std::chrono::seconds(1)){if(message.is_null()){message={{"action","activity"}};lastActivity=now;}else activity=1;}
            if(!message.is_null())sending=message.dump()+"\n";
        }
        if(!sending.empty()){auto count=send(pair[0],sending.data(),sending.size(),MSG_NOSIGNAL|MSG_DONTWAIT);if(count>0)sending.erase(0,size_t(count));else if(count<0&&errno!=EINTR&&errno!=EAGAIN)failed=true;}
        char buffer[4096];auto count=recv(pair[0],buffer,sizeof(buffer),MSG_DONTWAIT);if(count>0){receiving.append(buffer,size_t(count));if(receiving.size()>65536){failed=true;break;}size_t end;while((end=receiving.find('\n'))!=std::string::npos){auto line=receiving.substr(0,end);receiving.erase(0,end+1);try{auto update=Json::parse(line);if(update.is_object()&&update.count("connected")&&update.count("power")){std::lock_guard<std::mutex> lock(tvMutex);status=std::move(update);}}catch(const Json::exception&){Error("TV worker returned an invalid status.");}}}else if(!count||(errno!=EINTR&&errno!=EAGAIN))failed=true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    close(pair[0]);kill(child,SIGTERM);bool reaped=false;for(unsigned i=0;i<10;++i){if(waitpid(child,nullptr,WNOHANG)==child){reaped=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}if(!reaped){kill(child,SIGKILL);waitpid(child,nullptr,0);}if(failed&&!stopping)Error("TV worker stopped. Restart the app to restore TV control; input forwarding continues.");
}
}
void StartTvControl(){std::lock_guard<std::mutex> lock(tvMutex);if(tvThread.joinable())return;try{std::ifstream file(configPath);if(file.good()){Json value;file>>value;settings=Validate(value);}}catch(const std::exception&){status["error"]="Saved TV settings are invalid. Save them again.";}activityEnabled=settings["enabled"].get<bool>()&&settings["wakeOnInput"].get<bool>();movementEnabled=activityEnabled&&settings["wakeOnMouseMovement"].get<bool>();activity=0;motion=0;hostState=0;hostGeneration=0;stopping=false;tvThread=std::thread(Dispatch);}
void StopTvControl(){stopping=true;activityEnabled=false;if(tvThread.joinable())tvThread.join();}
void TvHostChanged(bool bluetooth,bool explicitSwitch){hostState=(bluetooth?1u:0u)|(explicitSwitch?2u:0u);++hostGeneration;}
void TvInputActivity(bool mouse,const uint8_t* report,size_t size){
    if(!activityEnabled||!report||size<2)return;
    if(mouse&&report[0]==1&&size==9){if(report[1]||report[2]||report[7]||report[8])activity=1;if(movementEnabled){int x=int16_t(uint16_t(report[3])|(uint16_t(report[4])<<8)),y=int16_t(uint16_t(report[5])|(uint16_t(report[6])<<8));if(x||y)motion.fetch_add(unsigned(abs(x)+abs(y)));}}
    else if(report[0]==1||report[0]==2||report[0]==3){for(size_t i=1;i<size;++i)if(report[i]){activity=1;break;}}
}
std::string GetTvStatus(){std::lock_guard<std::mutex> lock(tvMutex);return Json{{"settings",settings},{"status",status}}.dump();}
void UpdateTv(const std::string& body){
    try{auto request=Json::parse(body);auto action=request.at("action").get<std::string>();std::lock_guard<std::mutex> lock(tvMutex);
    if(action=="tvSettings"){auto value=Validate(request.at("settings"));if(commands.size()>=8)throw std::runtime_error("TV controls are busy. Please retry.");Save(value);settings=value;activityEnabled=value["enabled"].get<bool>()&&value["wakeOnInput"].get<bool>();movementEnabled=activityEnabled&&value["wakeOnMouseMovement"].get<bool>();Queue({{"action","config"},{"settings",settings}});}
    else if(action=="tvPair"||action=="tvWake"||action=="tvSync"||action=="tvForget"){if(!settings["enabled"].get<bool>()||settings["host"].get<std::string>().empty())throw std::invalid_argument("Save with TV control enabled and a TV address first.");Queue({{"action",action=="tvPair"?"pair":action=="tvWake"?"wake":action=="tvSync"?"sync":"forget"}});}
    else throw std::invalid_argument("Unknown TV action.");
    }catch(const Json::exception&){throw std::invalid_argument("Invalid TV settings.");}
}
