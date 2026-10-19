// Exercise the production HTTP query while the Bluetooth loop replaces devices.
#include "../../raspikey/bluetooth/bt.cpp"
#include <cassert>
#include <atomic>
#include <iostream>

namespace {
struct Device { DBusMessage* address; };
std::atomic<unsigned> reads{0};
std::atomic<unsigned> changes{0};
DBusMessage* Address(unsigned n) {
    auto* message = dbus_message_new_signal("/test", "org.raspikvm.Test", "Address");
    std::string address = "Device-" + std::to_string(n);
    const char* value = address.c_str();
    dbus_message_append_args(message, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
    return message;
}
gboolean ReplaceDevice(gpointer) {
    assert(g_main_context_is_owner(g_main_context_default()));
    if (bluetooth::g_devList) {
        auto* device = static_cast<Device*>(bluetooth::g_devList->data);
        dbus_message_unref(device->address);
        delete device;
        g_list_free(bluetooth::g_devList);
    }
    bluetooth::g_devList = g_list_append(nullptr, new Device{Address(++changes)});
    return G_SOURCE_CONTINUE;
}
}
Logger::Logger(){}
Logger::~Logger(){}
Logger& Logger::Instance(){static Logger instance;return instance;}
void Logger::Log(int,const char*,int,const char*){}
extern "C" gboolean g_dbus_proxy_get_property(GDBusProxy* proxy,const char* name,DBusMessageIter* iter) {
    // A request from the HTTP thread would access borrowed data concurrently.
    assert(g_main_context_is_owner(g_main_context_default()));
    ++reads;
    if (std::string(name) != "Address") return FALSE;
    return dbus_message_iter_init(reinterpret_cast<Device*>(proxy)->address, iter);
}
int main() {
    auto* loop = g_main_loop_new(nullptr, FALSE);
    const auto timer = g_timeout_add(1, ReplaceDevice, nullptr);
    std::thread bluetooth([&] { g_main_loop_run(loop); });
    std::vector<std::thread> clients;
    for (unsigned i = 0; i != 4; ++i) clients.emplace_back([] {
        for (unsigned n = 0; n != 200; ++n) {
            const auto devices = bluetooth::GetDevices();
            assert(devices.size() <= 1);
            if (!devices.empty()) assert(devices[0].Address.find("Device-") == 0);
        }
    });
    for (auto& client : clients) client.join();
    g_main_loop_quit(loop);
    bluetooth.join();
    g_source_remove(timer);
    if (bluetooth::g_devList) {
        auto* device = static_cast<Device*>(bluetooth::g_devList->data);
        dbus_message_unref(device->address);
        delete device;
        g_list_free(bluetooth::g_devList);
        bluetooth::g_devList = nullptr;
    }
    g_main_loop_unref(loop);
    assert(changes > 0 && reads > 0);
    std::cout << "800 concurrent production device queries stayed on the Bluetooth loop\n";
}
