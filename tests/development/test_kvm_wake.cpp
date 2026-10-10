// Capture synthetic wake notifications on a private D-Bus, never a real host.
#include "../../raspikey/KvmBluetooth.cpp"
#include <cassert>
#include <iostream>
namespace {bool hostSleeping=false,selected=true;}
bool KvmBluetoothEnabled(){return true;}
bool KvmPairingOpen(){return false;}
bool KvmAcceptHost(const std::string&){return true;}
std::string KvmHostAddress(){return {};}
void KvmHostSeen(const std::string&,const std::string&,bool,bool){}
void KvmHostLed(bool,uint8_t){}
void KvmBluetoothStatus(bool,bool,const std::string&){}
void KvmBluetoothSleeping(bool value){hostSleeping=value;}
bool KvmBluetoothSelected(){return selected;}

int main(){
    auto* testBus=g_test_dbus_new(G_TEST_DBUS_NONE);g_test_dbus_up(testBus);
    {
        Server server;GError* error=nullptr;
        auto flags=GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
        server.bus=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(testBus),flags,nullptr,nullptr,&error);assert(server.bus);
        server.info=g_dbus_node_info_new_for_xml(Xml,nullptr);server.Build();
        auto* keyboard=server.Find(std::string(Root)+"/hid/report1");auto* mouse=server.Find(std::string(Root)+"/hid/report2");
        keyboard->notify=true;mouse->notify=true;
        assert(server.Find(std::string(Root)+"/hid/info")->value[3]==3);
        server.application=true;server.connected=true;server.paired=true;
        auto* listener=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(testBus),flags,nullptr,nullptr,&error);assert(listener);
        std::vector<std::vector<uint8_t>> notifications;
        auto subscription=g_dbus_connection_signal_subscribe(listener,g_dbus_connection_get_unique_name(server.bus),Properties,"PropertiesChanged",keyboard->path.c_str(),nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* args,gpointer data){
                const gchar* interface;GVariant *changes,*invalidated;g_variant_get(args,"(&s@a{sv}@as)",&interface,&changes,&invalidated);(void)interface;
                auto* bytes=g_variant_lookup_value(changes,"Value",G_VARIANT_TYPE("ay"));
                if(bytes){gsize size=0;const auto* value=static_cast<const uint8_t*>(g_variant_get_fixed_array(bytes,&size,1));static_cast<std::vector<std::vector<uint8_t>>*>(data)->emplace_back(value,value+size);g_variant_unref(bytes);}
                g_variant_unref(changes);g_variant_unref(invalidated);
            },&notifications,nullptr);
        auto* barrier=g_dbus_connection_call_sync(listener,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","ListNames",nullptr,nullptr,G_DBUS_CALL_FLAGS_NONE,2000,nullptr,&error);assert(barrier);g_variant_unref(barrier);
        server.SetSuspend(true);assert(hostSleeping);
        advertisementChanged=false; // isolate notification test from BlueZ calls
        server.Deliver({1,{2,0,0,0,0,0,0,0}}); // sleeping ordinary input emits nothing
        assert(WakeKvmBluetoothHost());server.Drain();assert(!hostSleeping&&!server.suspended);
        auto deadline=g_get_monotonic_time()+2000000;
        while(notifications.size()<2&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(nullptr,false)){}g_usleep(1000);}
        assert(notifications.size()==2);
        assert(notifications[0]==std::vector<uint8_t>({2,0,0,0,0,0,0,0}));
        assert(notifications[1]==std::vector<uint8_t>(8));assert(keyboard->value==std::vector<uint8_t>(8));
        selected=false;server.SetSuspend(true);server.WakeHost();assert(server.suspended&&hostSleeping);
        assert(!advertisementRequested);
        g_dbus_connection_signal_unsubscribe(listener,subscription);g_dbus_connection_close_sync(listener,nullptr,nullptr);g_object_unref(listener);
    }
    g_test_dbus_down(testBus);g_object_unref(testBus);
    std::cout<<"RemoteWake flag and exactly one Shift press/release over GATT passed\n";
}
