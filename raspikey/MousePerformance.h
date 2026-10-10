#pragma once
#include <memory>
#include <string>
#include <cstdint>

class MousePerformance {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    MousePerformance();
    ~MousePerformance();
    void Connect(const std::string& address,bool mxMaster3s);
    void Disconnect();
    void Frame(uint64_t stamp,uint64_t before,uint64_t after);
    void Tick();
};
