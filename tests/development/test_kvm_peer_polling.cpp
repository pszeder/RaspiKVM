// Exercise the production loop and subscriptions against an independent BlueZ.
#include "../../raspikey/KvmBluetooth.cpp"
#include <cassert>
#include <functional>
#include <iostream>

namespace {
const std::string firstHost="00:11:22:33:44:55",secondHost="00:11:22:33:44:66";
std::mutex hostMutex;
std::string configuredHost=firstHost;
std::atomic<bool> statusRegistered{false},statusReady{false},hostSleeping{false};
std::atomic<unsigned> disconnectedEvents{0};
bool Until(const std::function<bool()>& predicate){
    auto deadline=g_get_monotonic_time()+5000000;
    while(!predicate()&&g_get_monotonic_time()<deadline)g_usleep(1000);
    return predicate();
}
void SetHost(const std::string& address){std::lock_guard<std::mutex> lock(hostMutex);configuredHost=address;}
}
bool KvmBluetoothEnabled(){return true;}
bool KvmPairingOpen(){return false;}
bool KvmAcceptHost(const std::string& address){return address==KvmHostAddress();}
std::string KvmHostAddress(){std::lock_guard<std::mutex> lock(hostMutex);return configuredHost;}
void KvmHostSeen(const std::string&,const std::string&,bool connected,bool){if(!connected)++disconnectedEvents;}
void KvmHostLed(bool,uint8_t){}
void KvmBluetoothStatus(bool active,bool ready,const std::string&){statusRegistered=active;statusReady=ready;}
void KvmBluetoothSleeping(bool sleeping){hostSleeping=sleeping;}
bool KvmBluetoothSelected(){return false;}

class FakeBluez {
public:
    struct Peer {std::atomic<bool> present{true},connected{true},paired{true};std::atomic<unsigned> queries{0};};
    Peer first,second;
    GDBusConnection* bus=nullptr;
    GMainContext* context=nullptr;
    GMainLoop* loop=nullptr;
    std::thread thread;
    std::atomic<bool> started{false};
    std::atomic<unsigned> registrations{0},advertisements{0},values{0};
    std::mutex nameMutex;
    std::string applicationName;
    static constexpr const char* xml=R"(<node>
      <interface name='org.bluez.AgentManager1'>
        <method name='RegisterAgent'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
        <method name='RequestDefaultAgent'><arg type='o' direction='in'/></method>
        <method name='UnregisterAgent'><arg type='o' direction='in'/></method>
      </interface>
      <interface name='org.bluez.GattManager1'>
        <method name='RegisterApplication'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>
        <method name='UnregisterApplication'><arg type='o' direction='in'/></method>
      </interface>
      <interface name='org.bluez.LEAdvertisingManager1'>
        <method name='RegisterAdvertisement'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>
        <method name='UnregisterAdvertisement'><arg type='o' direction='in'/></method>
      </interface>
      <interface name='org.freedesktop.DBus.Properties'>
        <method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method>
      </interface>
      <interface name='org.bluez.Device1'><method name='Disconnect'/></interface>
    </node>)";
    static void Method(GDBusConnection*,const gchar* sender,const gchar* path,const gchar*,const gchar* method,GVariant*,GDBusMethodInvocation* invocation,gpointer data){
        auto* self=static_cast<FakeBluez*>(data);
        if(!strcmp(method,"GetAll")){
            auto& peer=std::string(path)==HostPath(firstHost)?self->first:self->second;++peer.queries;
            if(!peer.present){g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.DoesNotExist","Device removed");return;}
            GVariantBuilder properties;g_variant_builder_init(&properties,G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&properties,"{sv}","Connected",g_variant_new_boolean(peer.connected));
            g_variant_builder_add(&properties,"{sv}","Paired",g_variant_new_boolean(peer.paired));
            g_variant_builder_add(&properties,"{sv}","Trusted",g_variant_new_boolean(true));
            g_variant_builder_add(&properties,"{sv}","Alias",g_variant_new_string("Test output"));
            g_dbus_method_invocation_return_value(invocation,g_variant_new("(@a{sv})",g_variant_builder_end(&properties)));return;
        }
        if(!strcmp(method,"RegisterApplication")){
            {std::lock_guard<std::mutex> lock(self->nameMutex);self->applicationName=sender;}
            ++self->registrations;
        }
        if(!strcmp(method,"RegisterAdvertisement"))++self->advertisements;
        g_dbus_method_invocation_return_value(invocation,nullptr);
    }
    GVariant* Call(const char* destination,const char* path,const char* interface,const char* method,GVariant* args){
        GError* error=nullptr;auto* reply=g_dbus_connection_call_sync(bus,destination,path,interface,method,args,nullptr,G_DBUS_CALL_FLAGS_NONE,3000,nullptr,&error);
        if(error){std::cerr<<method<<": "<<error->message<<'\n';g_error_free(error);}assert(reply);return reply;
    }
    void Own(bool acquire){auto* reply=Call("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus",acquire?"RequestName":"ReleaseName",acquire?g_variant_new("(su)","org.bluez",0u):g_variant_new("(s)","org.bluez"));g_variant_unref(reply);}
    explicit FakeBluez(const char* address){
        std::string busAddress=address;
        thread=std::thread([this,busAddress]{
            context=g_main_context_new();g_main_context_push_thread_default(context);loop=g_main_loop_new(context,false);
            GError* error=nullptr;bus=g_dbus_connection_new_for_address_sync(busAddress.c_str(),GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),nullptr,nullptr,&error);assert(bus);
            auto* info=g_dbus_node_info_new_for_xml(xml,nullptr);
            const GDBusInterfaceVTable methods={Method,nullptr,nullptr,{nullptr}};
            std::vector<guint> exports;
            auto add=[&](const std::string& path,const char* interface){auto id=g_dbus_connection_register_object(bus,path.c_str(),g_dbus_node_info_lookup_interface(info,interface),&methods,this,nullptr,&error);assert(id);exports.push_back(id);};
            add("/org/bluez","org.bluez.AgentManager1");add(Adapter,"org.bluez.GattManager1");add(Adapter,"org.bluez.LEAdvertisingManager1");
            for(const auto& host:{firstHost,secondHost}){add(HostPath(host),Properties);add(HostPath(host),"org.bluez.Device1");}
            auto watch=g_dbus_connection_signal_subscribe(bus,nullptr,Properties,"PropertiesChanged",nullptr,"org.bluez.GattCharacteristic1",G_DBUS_SIGNAL_FLAGS_NONE,
                [](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* args,gpointer data){auto* changes=g_variant_get_child_value(args,1);auto* value=g_variant_lookup_value(changes,"Value",nullptr);if(value){++static_cast<FakeBluez*>(data)->values;g_variant_unref(value);}g_variant_unref(changes);},this,nullptr);
            Own(true);started=true;g_main_loop_run(loop);
            g_dbus_connection_signal_unsubscribe(bus,watch);for(auto id:exports)g_dbus_connection_unregister_object(bus,id);
            g_dbus_node_info_unref(info);g_dbus_connection_close_sync(bus,nullptr,nullptr);g_object_unref(bus);
            g_main_loop_unref(loop);g_main_context_pop_thread_default(context);g_main_context_unref(context);
        });
        assert(Until([&]{return started.load();}));
    }
    ~FakeBluez(){g_main_loop_quit(loop);g_main_context_wakeup(context);thread.join();}
    void Property(const std::string& host,const char* name,bool value){
        GVariantBuilder changes;g_variant_builder_init(&changes,G_VARIANT_TYPE("a{sv}"));g_variant_builder_add(&changes,"{sv}",name,g_variant_new_boolean(value));
        assert(g_dbus_connection_emit_signal(bus,nullptr,HostPath(host).c_str(),Properties,"PropertiesChanged",g_variant_new("(s@a{sv}@as)","org.bluez.Device1",g_variant_builder_end(&changes),g_variant_new_strv(nullptr,0)),nullptr));
    }
    void Removed(const std::string& host,const char* interface="org.bluez.Device1"){
        const char* interfaces[]={interface,nullptr};assert(g_dbus_connection_emit_signal(bus,nullptr,"/","org.freedesktop.DBus.ObjectManager","InterfacesRemoved",g_variant_new("(o^as)",HostPath(host).c_str(),interfaces),nullptr));
    }
    void App(const std::string& suffix,const char* method,GVariant* args=nullptr){
        std::string name;{std::lock_guard<std::mutex> lock(nameMutex);name=applicationName;}
        auto* reply=Call(name.c_str(),(std::string(Root)+suffix).c_str(),"org.bluez.GattCharacteristic1",method,args);g_variant_unref(reply);
    }
    void Subscribe(){App("/hid/report1","StartNotify");App("/hid/report2","StartNotify");assert(Until([]{return statusReady.load();}));}
    void Suspend(bool suspend){uint8_t value=suspend?0:1;App("/hid/control","WriteValue",g_variant_new("(@ay@a{sv})",g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,&value,1,1),g_variant_new_array(G_VARIANT_TYPE("{sv}"),nullptr,0)));}
};
constexpr const char* FakeBluez::xml;

int main(){
    auto* testBus=g_test_dbus_new(G_TEST_DBUS_NONE);g_test_dbus_up(testBus);
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS",g_test_dbus_get_bus_address(testBus),true);
    {
        FakeBluez bluez(g_test_dbus_get_bus_address(testBus));StartKvmBluetooth();
        assert(Until([&]{return bluez.registrations.load()==1&&statusRegistered.load();}));bluez.Subscribe();
        auto queries=bluez.first.queries.load();g_usleep(2200000);assert(bluez.first.queries==queries);
        assert(bluez.advertisements==0&&bluez.values==0);

        bluez.first.connected=false;bluez.Property(firstHost,"Connected",false);assert(Until([]{return !statusReady.load();}));
        assert(Until([&]{return bluez.first.queries>queries;}));queries=bluez.first.queries;
        assert(Until([&]{return bluez.first.queries>queries;})); // disconnected polling continues
        bluez.first.connected=true;bluez.Property(firstHost,"Connected",true);assert(Until([]{return statusReady.load();})); // no resubscription
        bluez.first.paired=false;bluez.Property(firstHost,"Paired",false);assert(Until([]{return !statusReady.load();}));queries=bluez.first.queries;
        assert(Until([&]{return bluez.first.queries>queries;}));
        bluez.first.paired=true;bluez.Property(firstHost,"Paired",true);assert(Until([]{return statusReady.load();}));

        bluez.Suspend(true);assert(Until([]{return hostSleeping.load();}));queries=bluez.first.queries;
        g_usleep(1200000);assert(hostSleeping&&statusReady&&bluez.first.queries==queries);
        assert(bluez.advertisements==0&&bluez.values==0);bluez.Suspend(false);assert(!hostSleeping);

        // Neither another peer nor a non-Device1 interface invalidates this link.
        auto disconnected=disconnectedEvents.load();
        bluez.Removed(secondHost);bluez.Removed(firstHost,"org.bluez.Battery1");
        g_usleep(1200000);assert(statusReady&&disconnectedEvents==disconnected&&bluez.first.queries==queries);
        SetHost(secondHost);assert(Until([&]{return bluez.second.queries>0;}));assert(statusReady);
        bluez.Removed(firstHost);g_usleep(100000);assert(statusReady);
        bluez.second.present=false;bluez.Removed(secondHost);assert(Until([]{return !statusReady.load();}));assert(disconnectedEvents>disconnected);
        queries=bluez.second.queries;bluez.second.present=true;bluez.Property(secondHost,"Connected",true);
        assert(Until([&]{return bluez.second.queries>queries;}));
        assert(!statusReady);bluez.Subscribe(); // removal, unlike disconnect, clears subscriptions

        bluez.Own(false);assert(Until([]{return !statusRegistered.load()&&!statusReady.load();}));
        bluez.Own(true);assert(Until([&]{return bluez.registrations>=2&&statusRegistered.load();}));
        assert(!statusReady);bluez.Subscribe();queries=bluez.second.queries;
        g_usleep(1200000);assert(statusReady&&bluez.second.queries==queries);
        assert(bluez.advertisements==0&&bluez.values==0);
        StopKvmBluetooth();
    }
    g_unsetenv("DBUS_SYSTEM_BUS_ADDRESS");g_test_dbus_down(testBus);g_object_unref(testBus);
    std::cout<<"Production peer signals preserve reconnect/suspend behavior without established-link polling\n";
}
