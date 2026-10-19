// Drive the real legacy pairing agent through delayed/failing BlueZ replies.
#include "../../raspikey/bluetooth/agent.cpp"
#include <cassert>
#include <iostream>
#include <vector>

namespace {
struct Call {GDBusReturnFunction reply;void* data;GDBusDestroyFunction destroy;};
std::vector<Call> calls;
bool exported=false,failQueue=false;
void Reply(size_t index,bool success){
    auto call=calls.at(index);calls.erase(calls.begin()+index);
    auto* message=dbus_message_new(success?DBUS_MESSAGE_TYPE_METHOD_RETURN:DBUS_MESSAGE_TYPE_ERROR);
    if(!success)dbus_message_set_error_name(message,"org.bluez.Error.NotReady");
    call.reply(message,call.data);call.destroy(call.data);dbus_message_unref(message);
}
}
Logger::Logger(){}
Logger::~Logger(){}
Logger& Logger::Instance(){static Logger instance;return instance;}
void Logger::Log(int,const char*,int,const char*){}
namespace bluetooth {void OnAgentNotify(const std::string&) {}}
extern "C" {
gboolean g_dbus_register_interface(DBusConnection*,const char*,const char*,const GDBusMethodTable*,const GDBusSignalTable*,const GDBusPropertyTable*,void*,GDBusDestroyFunction){if(exported)return FALSE;exported=true;return TRUE;}
gboolean g_dbus_unregister_interface(DBusConnection*,const char*,const char*){bool previous=exported;exported=false;return previous;}
gboolean g_dbus_proxy_method_call(GDBusProxy*,const char* method,GDBusSetupFunction,GDBusReturnFunction reply,void* data,GDBusDestroyFunction destroy){
    assert(std::string(method)=="RegisterAgent");if(failQueue){failQueue=false;return FALSE;}
    calls.push_back({reply,data,destroy});return TRUE;
}
gboolean g_dbus_send_error(DBusConnection*,DBusMessage*,const char*,const char*,...){return TRUE;}
gboolean g_dbus_send_reply(DBusConnection*,DBusMessage*,int,...){return TRUE;}
}
int main(){
    using namespace bluetooth;
    auto* connection=reinterpret_cast<DBusConnection*>(1);
    auto* manager=reinterpret_cast<GDBusProxy*>(2);
    AgentRegister(connection,manager);AgentRegister(connection,manager);
    assert(exported&&g_AgentRegisterPending&&calls.size()==1); // either proxy order
    Reply(0,true);assert(g_AgentRegistered&&!g_AgentRegisterPending);
    AgentRegister(connection,manager);assert(calls.empty());
    g_PendingMessage=dbus_message_new_signal("/test","org.raspikvm.Test","Confirmation");
    AgentServiceLost(connection);assert(!exported&&!g_AgentRegistered&&!g_PendingMessage);
    AgentRegister(connection,manager);Reply(0,false);
    assert(!exported&&!g_AgentRegistered&&!g_AgentRegisterPending);
    AgentRegister(connection,manager);AgentServiceLost(connection);
    AgentRegister(connection,manager);assert(calls.size()==2&&exported);
    Reply(0,true);assert(!g_AgentRegistered&&g_AgentRegisterPending); // stale success
    Reply(0,true);assert(g_AgentRegistered&&!g_AgentRegisterPending);
    AgentServiceLost(connection);failQueue=true;AgentRegister(connection,manager);
    assert(!exported&&!g_AgentRegistered&&!g_AgentRegisterPending&&calls.empty());
    AgentRegister(connection,manager);AgentServiceLost(connection);AgentRegister(connection,manager);
    Reply(1,true);Reply(0,false);assert(exported&&g_AgentRegistered); // stale failure
    AgentServiceLost(connection);assert(!exported&&calls.empty());
    std::cout<<"Pairing agent re-registers after daemon loss and ignores stale callbacks\n";
}
