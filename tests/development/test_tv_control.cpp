#include "../../raspikey/TvControl.cpp"
#include <cassert>
#include <iostream>

int main(){
    auto config=settings;config["enabled"]=true;config["host"]="192.168.1.192";
    assert(Validate(config)["usbInput"]=="HDMI_4");
    for(const char* value:{"8.8.8.8","192.168.1.192/","127.0.0.1","bad"}){config["host"]=value;bool rejected=false;try{Validate(config);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
    activityEnabled=true;movementEnabled=true;
    const uint8_t release[9]={1},press[9]={1,0,0,4},pointer[9]={1,0,0,7},click[9]={1,1},wheel[9]={1,0,0,0,0,0,0,1};
    TvInputActivity(false,release,9);TvInputActivity(true,release,9);assert(!activity&&!motion);
    TvInputActivity(false,press,9);assert(activity.exchange(0)==1);
    TvInputActivity(true,pointer,9);assert(motion.exchange(0)==7&&!activity);
    TvInputActivity(true,click,9);assert(activity.exchange(0)==1);
    TvInputActivity(true,wheel,9);assert(activity.exchange(0)==1);
    movementEnabled=false;TvInputActivity(true,pointer,9);assert(!motion);
    activityEnabled=false;TvInputActivity(false,press,9);assert(!activity);
    // Smoke-test the actual embedded dependency, subprocess IPC and shutdown.
    StartTvControl();for(unsigned i=0;i<30;++i){if(nlohmann::json::parse(GetTvStatus())["status"].count("worker"))break;std::this_thread::sleep_for(std::chrono::milliseconds(100));}
    assert(nlohmann::json::parse(GetTvStatus())["status"].value("worker",false));
    assert(nlohmann::json::parse(GetTvStatus())["status"]["error"].get<std::string>().empty());
    StopTvControl();
    std::cout<<"TV settings, activity-only input hook and isolated worker smoke test passed\n";
}
