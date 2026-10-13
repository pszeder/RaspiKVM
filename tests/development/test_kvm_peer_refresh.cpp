// A delayed BlueZ control reply must not block keyboard press/release delivery.
#include "../../raspikey/KvmBluetooth.cpp"
#include <cassert>
#include <iostream>
#include <functional>
namespace {std::string address="00:11:22:33:44:55";}
bool KvmBluetoothEnabled(){return true;}
bool KvmPairingOpen(){return false;}
bool KvmAcceptHost(const std::string&){return true;}
std::string KvmHostAddress(){return address;}
void KvmHostSeen(const std::string&,const std::string&,bool,bool){}
void KvmHostLed(bool,uint8_t){}
void KvmBluetoothStatus(bool,bool,const std::string&){}
void KvmBluetoothSleeping(bool){}
bool KvmBluetoothSelected(){return true;}

static void Reply(GDBusMethodInvocation* invocation){
    GVariantBuilder properties;g_variant_builder_init(&properties,G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&properties,"{sv}","Connected",g_variant_new_boolean(true));
    g_variant_builder_add(&properties,"{sv}","Paired",g_variant_new_boolean(true));
    g_variant_builder_add(&properties,"{sv}","Alias",g_variant_new_string("Test computer"));
    g_dbus_method_invocation_return_value(invocation,g_variant_new("(@a{sv})",g_variant_builder_end(&properties)));
    g_object_unref(invocation);
}
static bool Until(const std::function<bool()>& done){
    auto deadline=g_get_monotonic_time()+2000000;
    while(!done()&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(nullptr,false)){}g_usleep(1000);}
    return done();
}
int main(){
    auto* testBus=g_test_dbus_new(G_TEST_DBUS_NONE);g_test_dbus_up(testBus);
    {
        GError* error=nullptr;
        auto flags=GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
        auto* bluez=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(testBus),flags,nullptr,nullptr,&error);assert(bluez);
        auto* name=g_dbus_connection_call_sync(bluez,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","RequestName",g_variant_new("(su)","org.bluez",0u),nullptr,G_DBUS_CALL_FLAGS_NONE,2000,nullptr,&error);assert(name);g_variant_unref(name);
        auto* info=g_dbus_node_info_new_for_xml("<node><interface name='org.freedesktop.DBus.Properties'><method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method></interface></node>",nullptr);
        GDBusMethodInvocation* pending=nullptr;unsigned queries=0;
        struct State{GDBusMethodInvocation** pending;unsigned* queries;} state{&pending,&queries};
        const GDBusInterfaceVTable methods={
            [](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant*,GDBusMethodInvocation* invocation,gpointer data){auto* state=static_cast<State*>(data);++*state->queries;*state->pending=static_cast<GDBusMethodInvocation*>(g_object_ref(invocation));},nullptr,nullptr,{nullptr}};
        auto registration=g_dbus_connection_register_object(bluez,HostPath(address).c_str(),info->interfaces[0],&methods,&state,nullptr,&error);assert(registration);
        Server server;server.bus=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(testBus),flags,nullptr,nullptr,&error);assert(server.bus);
        server.info=g_dbus_node_info_new_for_xml(Xml,nullptr);server.Build();server.application=true;server.connected=true;server.paired=true;
        auto* keyboard=server.Find(std::string(Root)+"/hid/report1");keyboard->notify=true;
        auto* listener=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(testBus),flags,nullptr,nullptr,&error);assert(listener);
        std::vector<std::vector<uint8_t>> reports;
        auto subscription=g_dbus_connection_signal_subscribe(listener,g_dbus_connection_get_unique_name(server.bus),Properties,"PropertiesChanged",keyboard->path.c_str(),nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* args,gpointer data){GVariant* changes=g_variant_get_child_value(args,1);auto* bytes=g_variant_lookup_value(changes,"Value",G_VARIANT_TYPE("ay"));if(bytes){gsize size;const auto* value=static_cast<const uint8_t*>(g_variant_get_fixed_array(bytes,&size,1));static_cast<std::vector<std::vector<uint8_t>>*>(data)->emplace_back(value,value+size);g_variant_unref(bytes);}g_variant_unref(changes);},&reports,nullptr);
        auto* barrier=g_dbus_connection_call_sync(listener,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","ListNames",nullptr,nullptr,G_DBUS_CALL_FLAGS_NONE,2000,nullptr,&error);assert(barrier);g_variant_unref(barrier);
        auto started=g_get_monotonic_time();server.RefreshPeer();assert(g_get_monotonic_time()-started<100000);
        assert(Until([&]{return pending!=nullptr;}));server.RefreshPeer();assert(queries==1);
        const uint8_t down[8]={0,0,0x37},up[8]={};
        assert(SendKvmBluetooth(1,down,8));assert(SendKvmBluetooth(1,up,8));server.Drain();
        assert(Until([&]{return reports.size()==2;}));
        assert(server.peerRefreshPending); // deliberately unanswered status query
        assert(reports[0]==std::vector<uint8_t>(down,down+8));assert(reports[1]==std::vector<uint8_t>(8));
        server.LinkClosed();Reply(pending);pending=nullptr;
        assert(Until([&]{return !server.peerRefreshPending;}));assert(!server.connected); // stale reply cannot resurrect a lost link
        server.RefreshPeer();assert(Until([&]{return pending!=nullptr;}));Reply(pending);pending=nullptr;
        assert(Until([&]{return !server.peerRefreshPending;}));assert(server.connected&&server.paired);
        g_dbus_connection_signal_unsubscribe(listener,subscription);g_dbus_connection_close_sync(listener,nullptr,nullptr);g_object_unref(listener);
        g_dbus_connection_unregister_object(bluez,registration);g_dbus_node_info_unref(info);g_dbus_connection_close_sync(bluez,nullptr,nullptr);g_object_unref(bluez);
    }
    g_test_dbus_down(testBus);g_object_unref(testBus);
    std::cout<<"Slow BlueZ status queries preserve keyboard edges; stale replies cannot reconnect a lost host\n";
}
