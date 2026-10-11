#pragma once
#include <memory>
class CpuPerformance {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    CpuPerformance();
    ~CpuPerformance();
};
