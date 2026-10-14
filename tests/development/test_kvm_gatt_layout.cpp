// Check the actual exported D-Bus layout across two application generations.
#include "../../raspikey/KvmBluetooth.cpp"
#include <cassert>
#include <iostream>

bool KvmBluetoothEnabled(){return true;}
namespace {bool pairingOpen=false;}
bool KvmPairingOpen(){return pairingOpen;}
bool KvmAcceptHost(const std::string&){return true;}
std::string KvmHostAddress(){return {};}
void KvmHostSeen(const std::string&,const std::string&,bool,bool){}
void KvmHostLed(bool,uint8_t){}
void KvmBluetoothStatus(bool,bool,const std::string&){}
void KvmBluetoothSleeping(bool){}
bool KvmBluetoothSelected(){return false;}

struct Reply {GVariant* value=nullptr;GError* error=nullptr;bool done=false;};
void Call(GDBusConnection* client,Server& server,const char* path,const char* interface,const char* method,GVariant* parameters,Reply& reply){
    g_dbus_connection_call(client,g_dbus_connection_get_unique_name(server.bus),path,interface,method,parameters,nullptr,G_DBUS_CALL_FLAGS_NONE,2000,nullptr,
        [](GObject* object,GAsyncResult* result,gpointer data){auto* reply=static_cast<Reply*>(data);reply->value=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),result,&reply->error);reply->done=true;},&reply);
    auto deadline=g_get_monotonic_time()+3000000;
    while(!reply.done&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(nullptr,false)){}g_usleep(1000);}
    assert(reply.done);
}
int main(){
    auto* testBus=g_test_dbus_new(G_TEST_DBUS_NONE);g_test_dbus_up(testBus);
    auto flags=GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
    GError* error=nullptr;auto* client=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(testBus),flags,nullptr,nullptr,&error);assert(client);
    GVariant* previous=nullptr;
    for(unsigned generation=0;generation<2;++generation){
        Server server;server.bus=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(testBus),flags,nullptr,nullptr,&error);assert(server.bus);
        server.info=g_dbus_node_info_new_for_xml(Xml,nullptr);server.Build();
        Reply objects;Call(client,server,Root,"org.freedesktop.DBus.ObjectManager","GetManagedObjects",nullptr,objects);assert(objects.value&&!objects.error);
        auto* layout=g_variant_get_child_value(objects.value,0);
        unsigned services=0;
        for(const auto& node:server.nodes){
            if(node->interface!="org.bluez.GattService1")continue;
            auto* interfaces=g_variant_lookup_value(layout,node->path.c_str(),G_VARIANT_TYPE("a{sa{sv}}"));assert(interfaces);
            auto* properties=g_variant_lookup_value(interfaces,node->interface.c_str(),G_VARIANT_TYPE("a{sv}"));assert(properties);
            guint16 handle=0;assert(g_variant_lookup(properties,"Handle","q",&handle)&&handle);
            assert(handle==node->handle);++services;
            // Account for declarations, values, implicit CCCs and descriptors
            // to ensure the fixed services have enough nonoverlapping space.
            unsigned span=1;
            for(const auto& child:server.nodes){
                if(child->interface=="org.bluez.GattCharacteristic1"&&child->parent==node->path){span+=2;if(std::find(child->flags.begin(),child->flags.end(),"notify")!=child->flags.end())++span;}
                if(child->interface=="org.bluez.GattDescriptor1"&&server.Find(child->parent)->parent==node->path)++span;
            }
            unsigned boundary=node->uuid=="1812"?0x0440:node->uuid=="180a"?0x0450:0x0460;
            assert(handle+span<=boundary);
            Reply same;Call(client,server,node->path.c_str(),Properties,"Set",g_variant_new("(ssv)",node->interface.c_str(),"Handle",g_variant_new_uint16(handle)),same);assert(same.value&&!same.error);g_variant_unref(same.value);
            Reply changed;Call(client,server,node->path.c_str(),Properties,"Set",g_variant_new("(ssv)",node->interface.c_str(),"Handle",g_variant_new_uint16(handle+1)),changed);assert(!changed.value&&g_error_matches(changed.error,G_DBUS_ERROR,G_DBUS_ERROR_INVALID_ARGS));g_clear_error(&changed.error);
            g_variant_unref(properties);g_variant_unref(interfaces);
        }
        assert(services==3);
        auto* map=server.Find(std::string(Root)+"/hid/map");assert(map&&map->handle==0);
        Reply assigned;Call(client,server,map->path.c_str(),Properties,"Set",g_variant_new("(ssv)",map->interface.c_str(),"Handle",g_variant_new_uint16(0x0402)),assigned);assert(assigned.value&&!assigned.error&&map->handle==0x0402);g_variant_unref(assigned.value);
        Reply replaced;Call(client,server,map->path.c_str(),Properties,"Set",g_variant_new("(ssv)",map->interface.c_str(),"Handle",g_variant_new_uint16(0x0403)),replaced);assert(!replaced.value&&replaced.error&&map->handle==0x0402);g_clear_error(&replaced.error);
        for(const auto& method: {"RequestAuthorization","RequestConfirmation"}){
            auto parameters=[&](){return !strcmp(method,"RequestAuthorization")?g_variant_new("(o)",HostPath("FC:B2:14:77:1B:F3").c_str()):g_variant_new("(ou)",HostPath("FC:B2:14:77:1B:F3").c_str(),123456u);};
            Reply closed;Call(client,server,(std::string(Root)+"/agent").c_str(),"org.bluez.Agent1",method,parameters(),closed);assert(!closed.value&&closed.error);g_clear_error(&closed.error);
            pairingOpen=true;Reply opened;Call(client,server,(std::string(Root)+"/agent").c_str(),"org.bluez.Agent1",method,parameters(),opened);assert(opened.value&&!opened.error);g_variant_unref(opened.value);pairingOpen=false;
        }
        if(previous){assert(g_variant_equal(previous,layout));g_variant_unref(previous);}
        previous=layout;g_variant_unref(objects.value);
    }
    g_variant_unref(previous);g_dbus_connection_close_sync(client,nullptr,nullptr);g_object_unref(client);g_test_dbus_down(testBus);g_object_unref(testBus);
    std::cout<<"Fixed, nonoverlapping HID service layout survives application recreation over D-Bus\n";
}
