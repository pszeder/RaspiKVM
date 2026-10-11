#pragma once
#include <memory>
#include <cstdint>

class KeyboardPerformance {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit KeyboardPerformance(int eventFd);
    ~KeyboardPerformance();
    int EventFd() const;
    void Drain();
    void Report(uint64_t readAt,uint64_t filteredAt,uint64_t queuedAt);
    void Tick();
};
