#include "KvmBluetooth.h"
#include "KvmRouter.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <memory>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
const char* Root="/org/raspikey/kvm";
const char* Adapter="/org/bluez/hci0";
const char* Properties="org.freedesktop.DBus.Properties";
std::atomic<bool> stopping{false};
std::thread worker;
std::mutex queueMutex;
struct Report{uint8_t id;std::vector<uint8_t> bytes;};
std::deque<Report> queue;
std::string forgetHost;
int wake=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
const uint8_t Map[]={
    // Keyboard ID 1: modifiers, reserved byte, six keys, LED output.
    0x05,1,0x09,6,0xa1,1,0x85,1,0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x95,1,0x75,8,0x81,3,0x19,0,0x29,0x6d,0x15,0,0x25,0x6d,0x95,6,0x81,0,
    0x05,8,0x19,1,0x29,5,0x15,0,0x25,1,0x75,1,0x95,5,0x91,2,0x75,3,0x95,1,0x91,3,0xc0,
    // Mouse ID 2: sixteen buttons, signed 16-bit XY, wheel and AC Pan.
    0x05,1,0x09,2,0xa1,1,0x85,2,0x09,1,0xa1,0,0x05,9,0x19,1,0x29,16,0x15,0,0x25,1,0x75,1,0x95,16,0x81,2,
    0x05,1,0x09,0x30,0x09,0x31,0x16,1,0x80,0x26,0xff,0x7f,0x75,16,0x95,2,0x81,6,
    0x09,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,1,0x81,6,0x05,0x0c,0x0a,0x38,2,0x81,6,0xc0,0xc0,
    // Consumer ID 3: one standard consumer usage.
    0x05,0x0c,0x09,1,0xa1,1,0x85,3,0x19,0,0x2a,0xff,3,0x15,0,0x26,0xff,3,0x75,16,0x95,1,0x81,0,0xc0
};
const char* Xml=R"(<node>
<interface name='org.freedesktop.DBus.ObjectManager'><method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method></interface>
<interface name='org.bluez.GattService1'><property name='UUID' type='s' access='read'/><property name='Primary' type='b' access='read'/><property name='Includes' type='ao' access='read'/></interface>
<interface name='org.bluez.GattCharacteristic1'>
<method name='ReadValue'><arg type='a{sv}' direction='in'/><arg type='ay' direction='out'/></method>
<method name='WriteValue'><arg type='ay' direction='in'/><arg type='a{sv}' direction='in'/></method>
<method name='StartNotify'/><method name='StopNotify'/>
<property name='UUID' type='s' access='read'/><property name='Service' type='o' access='read'/><property name='Flags' type='as' access='read'/><property name='Value' type='ay' access='read'/><property name='Notifying' type='b' access='read'/></interface>
<interface name='org.bluez.GattDescriptor1'><method name='ReadValue'><arg type='a{sv}' direction='in'/><arg type='ay' direction='out'/></method><property name='UUID' type='s' access='read'/><property name='Characteristic' type='o' access='read'/><property name='Flags' type='as' access='read'/></interface>
<interface name='org.bluez.LEAdvertisement1'><method name='Release'/><property name='Type' type='s' access='read'/><property name='ServiceUUIDs' type='as' access='read'/><property name='LocalName' type='s' access='read'/><property name='Appearance' type='q' access='read'/><property name='Discoverable' type='b' access='read'/></interface>
<interface name='org.bluez.Agent1'>
<method name='Release'/><method name='Cancel'/>
<method name='RequestAuthorization'><arg type='o' direction='in'/></method>
<method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>
<method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
<method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>
<method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>
<method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
<method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/></method>
</interface></node>)";
GVariant* Bytes(const std::vector<uint8_t>& bytes){return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,bytes.data(),bytes.size(),1);}
std::string HostPath(const std::string& address){auto result=std::string(Adapter)+"/dev_"+address;std::replace(result.begin(),result.end(),':','_');return result;}
std::string HostAddress(const std::string& path){auto pos=path.find("/dev_");if(pos==std::string::npos)return "";auto result=path.substr(pos+5);if(result.size()!=17)return "";std::replace(result.begin(),result.end(),'_',':');return result;}
struct Node{
    std::string path,interface,uuid,parent;
    std::vector<std::string> flags;
    std::vector<uint8_t> value;
    uint8_t report=0;
    bool notify=false,boot=false;
};
class Server {
public:
    GDBusConnection* bus=nullptr;GMainContext* context=nullptr;GMainLoop* loop=nullptr;GDBusNodeInfo* info=nullptr;
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<guint> registrations;
    bool application=false,advertisement=false,agent=false,discoverable=false,paired=false,connected=false;
    uint8_t protocol=1;bool suspended=false;
    std::string observedHost;
    GVariant* Call(const char* path,const char* interface,const char* method,GVariant* args){GError* error=nullptr;auto result=g_dbus_connection_call_sync(bus,"org.bluez",path,interface,method,args,nullptr,G_DBUS_CALL_FLAGS_NONE,3000,nullptr,&error);if(!result){std::string message=error?error->message:"Bluetooth call failed";g_clear_error(&error);throw std::runtime_error(message);}return result;}
    void Command(const char* path,const char* interface,const char* method,GVariant* args){auto result=Call(path,interface,method,args);g_variant_unref(result);}
    static void Method(GDBusConnection*,const gchar*,const gchar* path,const gchar* interface,const gchar* method,GVariant* args,GDBusMethodInvocation* invocation,gpointer data){try{static_cast<Server*>(data)->Dispatch(path,interface,method,args,invocation);}catch(const std::exception& ex){g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Failed",ex.what());}}
    static GVariant* Property(GDBusConnection*,const gchar*,const gchar* path,const gchar*,const gchar* property,GError**,gpointer data){return static_cast<Server*>(data)->PropertyValue(path,property);}
    void Export(const std::string& path,const char* interface){static const GDBusInterfaceVTable table={Method,Property,nullptr,{nullptr}};GError* error=nullptr;guint id=g_dbus_connection_register_object(bus,path.c_str(),g_dbus_node_info_lookup_interface(info,interface),&table,this,nullptr,&error);if(!id){std::string text=error->message;g_error_free(error);throw std::runtime_error(text);}registrations.push_back(id);}
    Node& Add(const std::string& path,const char* interface,const char* uuid,const std::string& parent={},std::vector<std::string> flags={},std::vector<uint8_t> value={},uint8_t report=0,bool boot=false){std::unique_ptr<Node> node(new Node{path,interface,uuid,parent,flags,value,report,false,boot});nodes.push_back(std::move(node));Export(path,interface);return *nodes.back();}
    Node* Find(const std::string& path){for(auto& node:nodes)if(node->path==path)return node.get();return nullptr;}
    GVariant* PropertyValue(const std::string& path,const std::string& property){
        if(path==std::string(Root)+"/advertisement"){
            if(property=="Type")return g_variant_new_string("peripheral");
            if(property=="LocalName")return g_variant_new_string("RaspiKey KVM");
            if(property=="Appearance")return g_variant_new_uint16(0x03c1);
            if(property=="Discoverable")return g_variant_new_boolean(discoverable);
            if(property=="ServiceUUIDs"){const char* uuids[]={"1812",nullptr};return g_variant_new_strv(uuids,-1);}return nullptr;
        }
        auto* node=Find(path);if(!node)return nullptr;
        if(property=="UUID")return g_variant_new_string(node->uuid.c_str());
        if(property=="Primary")return g_variant_new_boolean(true);
        if(property=="Includes")return g_variant_new_objv(nullptr,0);
        if(property=="Service"||property=="Characteristic")return g_variant_new_object_path(node->parent.c_str());
        if(property=="Value")return Bytes(node->value);
        if(property=="Notifying")return g_variant_new_boolean(node->notify);
        if(property=="Flags"){std::vector<const char*> values;for(auto& flag:node->flags)values.push_back(flag.c_str());return g_variant_new_strv(values.data(),values.size());}return nullptr;
    }
    GVariant* NodeProperties(Node& node){GVariantBuilder result;g_variant_builder_init(&result,G_VARIANT_TYPE("a{sv}"));
        for(const char* property: {"UUID","Primary","Includes","Service","Characteristic","Flags","Value","Notifying"}){
            bool service=node.interface=="org.bluez.GattService1",descriptor=node.interface=="org.bluez.GattDescriptor1";
            if(service&&strcmp(property,"UUID")&&strcmp(property,"Primary")&&strcmp(property,"Includes"))continue;
            if(!service&&(!strcmp(property,"Primary")||!strcmp(property,"Includes")))continue;
            if(descriptor&&(!strcmp(property,"Service")||!strcmp(property,"Notifying")||!strcmp(property,"Value")))continue;
            if(!descriptor&&!strcmp(property,"Characteristic"))continue;
            g_variant_builder_add(&result,"{sv}",property,PropertyValue(node.path,property));
        }return g_variant_builder_end(&result);
    }
    void Changed(Node& node,const char* property,GVariant* value){GVariantBuilder changes;g_variant_builder_init(&changes,G_VARIANT_TYPE("a{sv}"));g_variant_builder_add(&changes,"{sv}",property,value);g_dbus_connection_emit_signal(bus,nullptr,node.path.c_str(),Properties,"PropertiesChanged",g_variant_new("(s@a{sv}@as)",node.interface.c_str(),g_variant_builder_end(&changes),g_variant_new_strv(nullptr,0)),nullptr);}
    void RefreshPeer(){auto address=KvmHostAddress();if(address.empty()){paired=false;connected=false;return;}observedHost=address;
        try{auto response=Call(HostPath(address).c_str(),Properties,"GetAll",g_variant_new("(s)","org.bluez.Device1"));GVariant* properties;g_variant_get(response,"(@a{sv})",&properties);gboolean c=false,p=false;const gchar* alias="Bluetooth computer";g_variant_lookup(properties,"Connected","b",&c);g_variant_lookup(properties,"Paired","b",&p);g_variant_lookup(properties,"Alias","&s",&alias);connected=c;paired=p;KvmHostSeen(address,alias,c,p);g_variant_unref(properties);g_variant_unref(response);if(!connected)for(auto& node:nodes)node->notify=false;
        }catch(const std::exception&){connected=false;paired=false;KvmHostSeen(address,"",false,false);for(auto& node:nodes)node->notify=false;}
    }
    void Status(){bool keyboard=false,mouse=false;for(auto& node:nodes)if(node->notify&&node->boot==(protocol==0)){keyboard|=node->report==1;mouse|=node->report==2;}KvmBluetoothStatus(application,connected&&paired&&keyboard&&mouse&&!suspended,"");}
    void Observe(GVariant* options){const gchar* device=nullptr;if(g_variant_lookup(options,"device","&o",&device)){auto address=HostAddress(device);if(address.empty()||!KvmAcceptHost(address))throw std::runtime_error("This Bluetooth output host is not authorized. Open pairing in the web UI.");RefreshPeer();}}
    void Dispatch(const std::string& path,const std::string& interface,const std::string& method,GVariant* args,GDBusMethodInvocation* invocation){
        if(interface=="org.freedesktop.DBus.ObjectManager"){
            GVariantBuilder objects;g_variant_builder_init(&objects,G_VARIANT_TYPE("a{oa{sa{sv}}}"));
            for(auto& node:nodes){GVariantBuilder interfaces;g_variant_builder_init(&interfaces,G_VARIANT_TYPE("a{sa{sv}}"));g_variant_builder_add(&interfaces,"{s@a{sv}}",node->interface.c_str(),NodeProperties(*node));g_variant_builder_add(&objects,"{o@a{sa{sv}}}",node->path.c_str(),g_variant_builder_end(&interfaces));}
            g_dbus_method_invocation_return_value(invocation,g_variant_new("(@a{oa{sa{sv}}})",g_variant_builder_end(&objects)));return;
        }
        if(interface=="org.bluez.Agent1"){
            if(method=="RequestPinCode"||method=="RequestPasskey"){g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Rejected","Bluetooth output uses LE pairing.");return;}
            if(method=="RequestAuthorization"||method=="RequestConfirmation"||method=="AuthorizeService"){
                auto first=g_variant_get_child_value(args,0);auto address=HostAddress(g_variant_get_string(first,nullptr));g_variant_unref(first);
                if(address.empty()||!KvmAcceptHost(address)){g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Rejected","Open the RaspiKey host pairing window first.");return;}
            }
            g_dbus_method_invocation_return_value(invocation,nullptr);return;
        }
        if(interface=="org.bluez.LEAdvertisement1"){advertisement=false;g_dbus_method_invocation_return_value(invocation,nullptr);return;}
        auto* node=Find(path);if(!node)throw std::runtime_error("Unknown HID attribute.");
        if(method=="ReadValue"){
            GVariant* options;g_variant_get(args,"(@a{sv})",&options);try{Observe(options);}catch(...){g_variant_unref(options);throw;}
            guint16 offset=0;g_variant_lookup(options,"offset","q",&offset);g_variant_unref(options);
            if(offset>node->value.size()){g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.InvalidOffset","Invalid offset.");return;}
            std::vector<uint8_t> value(node->value.begin()+offset,node->value.end());g_dbus_method_invocation_return_value(invocation,g_variant_new("(@ay)",Bytes(value)));Status();return;
        }
        if(method=="WriteValue"){
            if(std::find(node->flags.begin(),node->flags.end(),"write")==node->flags.end()&&std::find(node->flags.begin(),node->flags.end(),"write-without-response")==node->flags.end()){g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.NotPermitted","Read-only report.");return;}
            GVariant* bytes;GVariant* options;g_variant_get(args,"(@ay@a{sv})",&bytes,&options);
            try{Observe(options);}catch(...){g_variant_unref(bytes);g_variant_unref(options);throw;}g_variant_unref(options);gsize count=0;const auto* value=static_cast<const uint8_t*>(g_variant_get_fixed_array(bytes,&count,1));
            if(count!=1||(node->uuid=="2a4e"&&value[0]>1)||(node->uuid=="2a4c"&&value[0]>1)){g_variant_unref(bytes);g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.InvalidValueLength","Expected one valid byte.");return;}
            node->value.assign(value,value+count);if(node->uuid=="2a4e")protocol=value[0];if(node->uuid=="2a4c")suspended=value[0]==0;if(node->uuid=="2a32"||node->path==std::string(Root)+"/hid/led")KvmHostLed(true,value[0]);g_variant_unref(bytes);Status();g_dbus_method_invocation_return_value(invocation,nullptr);return;
        }
        if(method=="StartNotify"||method=="StopNotify"){
            if(std::find(node->flags.begin(),node->flags.end(),"notify")==node->flags.end()){g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.NotSupported","Not a notification report.");return;}
            node->notify=method=="StartNotify";Changed(*node,"Notifying",g_variant_new_boolean(node->notify));RefreshPeer();Status();g_dbus_method_invocation_return_value(invocation,nullptr);return;
        }
        g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.NotSupported","Unsupported HID operation.");
    }
    void Build(){
        Export(Root,"org.freedesktop.DBus.ObjectManager");
        const std::string hid=std::string(Root)+"/hid",device=std::string(Root)+"/device",battery=std::string(Root)+"/battery";
        Add(hid,"org.bluez.GattService1","1812");Add(device,"org.bluez.GattService1","180a");Add(battery,"org.bluez.GattService1","180f");
        Add(hid+"/map","org.bluez.GattCharacteristic1","2a4b",hid,{"read"},{std::begin(Map),std::end(Map)});
        Add(hid+"/info","org.bluez.GattCharacteristic1","2a4a",hid,{"read"},{0x11,1,0,2});
        Add(hid+"/control","org.bluez.GattCharacteristic1","2a4c",hid,{"write-without-response","encrypt-write"},{1});
        Add(hid+"/protocol","org.bluez.GattCharacteristic1","2a4e",hid,{"read","write-without-response","encrypt-write"},{1});
        for(uint8_t id:{1,2,3}){auto name=hid+"/report"+std::to_string(id);Add(name,"org.bluez.GattCharacteristic1","2a4d",hid,{"read","notify","encrypt-read"},std::vector<uint8_t>(id==3?2:8),id);Add(name+"/reference","org.bluez.GattDescriptor1","2908",name,{"read"},{id,1});}
        Add(hid+"/led","org.bluez.GattCharacteristic1","2a4d",hid,{"read","write","write-without-response","encrypt-read","encrypt-write"},{0});Add(hid+"/led/reference","org.bluez.GattDescriptor1","2908",hid+"/led",{"read"},{1,2});
        Add(hid+"/bootKeyboard","org.bluez.GattCharacteristic1","2a22",hid,{"read","notify","encrypt-read"},std::vector<uint8_t>(8),1,true);
        Add(hid+"/bootMouse","org.bluez.GattCharacteristic1","2a33",hid,{"read","notify","encrypt-read"},std::vector<uint8_t>(3),2,true);
        Add(hid+"/bootLed","org.bluez.GattCharacteristic1","2a32",hid,{"read","write","write-without-response","encrypt-read","encrypt-write"},{0});
        Add(device+"/pnp","org.bluez.GattCharacteristic1","2a50",device,{"read"},{2,0x6b,0x1d,4,1,1,0});
        Add(device+"/manufacturer","org.bluez.GattCharacteristic1","2a29",device,{"read"},{'R','a','s','p','i','K','e','y'});
        Add(battery+"/level","org.bluez.GattCharacteristic1","2a19",battery,{"read"},{100});
        Export(std::string(Root)+"/advertisement","org.bluez.LEAdvertisement1");Export(std::string(Root)+"/agent","org.bluez.Agent1");
    }
    void Tick(){
        if(stopping){Drain();g_main_loop_quit(loop);return;}
        bool enabled=KvmBluetoothEnabled(),pairing=KvmPairingOpen();
        try{
            if(!enabled){if(advertisement){Command(Adapter,"org.bluez.LEAdvertisingManager1","UnregisterAdvertisement",g_variant_new("(o)",(std::string(Root)+"/advertisement").c_str()));advertisement=false;}if(application){Command(Adapter,"org.bluez.GattManager1","UnregisterApplication",g_variant_new("(o)",Root));application=false;}if(agent){Command("/org/bluez","org.bluez.AgentManager1","UnregisterAgent",g_variant_new("(o)",(std::string(Root)+"/agent").c_str()));agent=false;}for(auto& node:nodes)node->notify=false;KvmBluetoothStatus(false,false,"");return;}
            if(!agent){Command("/org/bluez","org.bluez.AgentManager1","RegisterAgent",g_variant_new("(os)",(std::string(Root)+"/agent").c_str(),"NoInputNoOutput"));agent=true;Command("/org/bluez","org.bluez.AgentManager1","RequestDefaultAgent",g_variant_new("(o)",(std::string(Root)+"/agent").c_str()));}
            if(!application){
                // Asynchronous registration: BlueZ must call our ObjectManager
                // while RegisterApplication is pending on this same main loop.
                application=true;
                g_dbus_connection_call(bus,"org.bluez",Adapter,"org.bluez.GattManager1","RegisterApplication",g_variant_new("(o@a{sv})",Root,g_variant_new_array(G_VARIANT_TYPE("{sv}"),nullptr,0)),nullptr,G_DBUS_CALL_FLAGS_NONE,10000,nullptr,[](GObject* object,GAsyncResult* result,gpointer data){auto* self=static_cast<Server*>(data);GError* error=nullptr;auto reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),result,&error);if(!reply){self->application=false;KvmBluetoothStatus(false,false,error->message);g_error_free(error);}else{g_variant_unref(reply);self->Status();}},this);
            }
            if(advertisement&&pairing!=discoverable){Command(Adapter,"org.bluez.LEAdvertisingManager1","UnregisterAdvertisement",g_variant_new("(o)",(std::string(Root)+"/advertisement").c_str()));advertisement=false;}
            if(!advertisement){discoverable=pairing;advertisement=true;g_dbus_connection_call(bus,"org.bluez",Adapter,"org.bluez.LEAdvertisingManager1","RegisterAdvertisement",g_variant_new("(o@a{sv})",(std::string(Root)+"/advertisement").c_str(),g_variant_new_array(G_VARIANT_TYPE("{sv}"),nullptr,0)),nullptr,G_DBUS_CALL_FLAGS_NONE,10000,nullptr,[](GObject* object,GAsyncResult* result,gpointer data){auto* self=static_cast<Server*>(data);GError* error=nullptr;auto reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),result,&error);if(!reply){self->advertisement=false;KvmBluetoothStatus(self->application,false,error->message);g_error_free(error);}else g_variant_unref(reply);},this);}
            RefreshPeer();Status();
        }catch(const std::exception& ex){KvmBluetoothStatus(application,false,ex.what());}
    }
    void Drain(){uint64_t count;while(read(wake,&count,sizeof(count))>0){}std::deque<Report> reports;std::string forgotten;{std::lock_guard<std::mutex> lock(queueMutex);reports.swap(queue);forgotten.swap(forgetHost);}
        if(!forgotten.empty()){for(auto& node:nodes)node->notify=false;connected=false;paired=false;observedHost.clear();try{Command(Adapter,"org.bluez.Adapter1","RemoveDevice",g_variant_new("(o)",HostPath(forgotten).c_str()));}catch(const std::exception& ex){KvmBluetoothStatus(application,false,ex.what());}reports.clear();}
        if(!application||!connected||suspended)return;
        for(auto& report:reports)for(auto& node:nodes)if(node->report==report.id&&node->notify&&node->boot==(protocol==0)){
            if(node->boot&&report.id==2){if(report.bytes.size()<6)continue;int16_t x=int16_t(uint16_t(report.bytes[2])|(uint16_t(report.bytes[3])<<8)),y=int16_t(uint16_t(report.bytes[4])|(uint16_t(report.bytes[5])<<8));node->value={uint8_t(report.bytes[0]&7),uint8_t(std::max(-127,std::min(127,int(x)))),uint8_t(std::max(-127,std::min(127,int(y))))};}
            else node->value=report.bytes;
            Changed(*node,"Value",Bytes(node->value));
        }
    }
    void Run(){
        context=g_main_context_new();g_main_context_push_thread_default(context);loop=g_main_loop_new(context,false);info=g_dbus_node_info_new_for_xml(Xml,nullptr);
        GError* error=nullptr;gchar* address=g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM,nullptr,&error);if(!address)throw std::runtime_error("System Bluetooth bus is unavailable.");bus=g_dbus_connection_new_for_address_sync(address,GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),nullptr,nullptr,&error);g_free(address);if(!bus)throw std::runtime_error("Could not connect to Bluetooth bus.");
        Build();
        // React to host disconnects immediately instead of waiting for the
        // periodic reconnect/status check. Ignore source keyboard/mouse peers.
        auto peerWatch=g_dbus_connection_signal_subscribe(bus,"org.bluez",Properties,"PropertiesChanged",nullptr,"org.bluez.Device1",G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*,const gchar*,const gchar* path,const gchar*,const gchar*,GVariant*,gpointer data){auto* self=static_cast<Server*>(data);auto address=HostAddress(path);if(!address.empty()&&address==KvmHostAddress()){self->RefreshPeer();self->Status();}},this,nullptr);
        auto daemonWatch=g_dbus_connection_signal_subscribe(bus,"org.freedesktop.DBus", "org.freedesktop.DBus","NameOwnerChanged","/org/freedesktop/DBus","org.bluez",G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* args,gpointer data){auto* self=static_cast<Server*>(data);const gchar *name,*oldOwner,*newOwner;g_variant_get(args,"(&s&s&s)",&name,&oldOwner,&newOwner);(void)name;(void)oldOwner;if(!*newOwner){self->application=false;self->advertisement=false;self->agent=false;self->connected=false;self->paired=false;for(auto& node:self->nodes)node->notify=false;KvmBluetoothStatus(false,false,"Bluetooth restarted. Reconnecting output host.");}},this,nullptr);
        auto* timer=g_timeout_source_new(1000);g_source_set_callback(timer,[](gpointer data)->gboolean{static_cast<Server*>(data)->Tick();return G_SOURCE_CONTINUE;},this,nullptr);g_source_attach(timer,context);
        auto* source=g_unix_fd_source_new(wake,G_IO_IN);g_source_set_callback(source,G_SOURCE_FUNC(+[](gint,GIOCondition,gpointer data)->gboolean{static_cast<Server*>(data)->Drain();return G_SOURCE_CONTINUE;}),this,nullptr);g_source_attach(source,context);
        g_main_loop_run(loop);g_source_destroy(timer);g_source_unref(timer);g_source_destroy(source);g_source_unref(source);
        g_dbus_connection_signal_unsubscribe(bus,peerWatch);g_dbus_connection_signal_unsubscribe(bus,daemonWatch);
    }
    ~Server(){if(bus){g_dbus_connection_close_sync(bus,nullptr,nullptr);g_object_unref(bus);}if(info)g_dbus_node_info_unref(info);if(loop)g_main_loop_unref(loop);if(context){g_main_context_pop_thread_default(context);g_main_context_unref(context);}}
};
}
void StartKvmBluetooth(){if(worker.joinable())return;stopping=false;worker=std::thread([]{while(!stopping){try{Server server;server.Run();}catch(const std::exception& ex){KvmBluetoothStatus(false,false,ex.what());for(unsigned i=0;i<20&&!stopping;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));}}});}
void StopKvmBluetooth(){stopping=true;uint64_t one=1;write(wake,&one,sizeof(one));if(worker.joinable())worker.join();std::lock_guard<std::mutex> lock(queueMutex);queue.clear();}
bool SendKvmBluetooth(uint8_t id,const uint8_t* data,size_t size){if(stopping||wake<0)return false;std::lock_guard<std::mutex> lock(queueMutex);if(queue.size()>=256){queue.clear();const uint8_t zero[8]={};for(uint8_t report:{1,2,3})queue.push_back({report,{zero,zero+(report==3?2:8)}});uint64_t one=1;write(wake,&one,sizeof(one));return false;}queue.push_back({id,{data,data+size}});uint64_t one=1;write(wake,&one,sizeof(one));return true;}
void ForgetKvmBluetoothHost(const char* address){std::lock_guard<std::mutex> lock(queueMutex);forgetHost=address;uint64_t one=1;write(wake,&one,sizeof(one));}
