#pragma once
#include <atomic>
#include <string>
#include <thread>

struct MouseBridgeStatus {
    bool usbReady=false, connected=false;
    std::string address, name;
    uint64_t reports=0, lastForwardUs=0, maxForwardUs=0, droppedFrames=0;
};
MouseBridgeStatus GetMouseBridgeStatus();
class MouseBridge {
    std::atomic<bool> stopping{false};
    std::thread worker;
    std::thread controlWorker;
    void Run();
public:
    void Start();
    void Stop();
    ~MouseBridge() {Stop();}
};
