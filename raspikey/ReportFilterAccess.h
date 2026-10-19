#pragma once
#include "ReportFilter.h"
#include <mutex>
#include <stdexcept>

inline std::string ReadFilterSettings(std::mutex& mutex, IReportFilter*& filter) {
    std::lock_guard<std::mutex> guard(mutex);
    if (!filter) throw std::runtime_error("Keyboard is disconnected; settings are unavailable");
    return filter->GetSettings();
}
inline void WriteFilterSettings(std::mutex& mutex, IReportFilter*& filter, const std::string& settings) {
    std::lock_guard<std::mutex> guard(mutex);
    if (!filter) throw std::runtime_error("Keyboard is disconnected; settings are unavailable");
    filter->SetSettings(settings);
}
