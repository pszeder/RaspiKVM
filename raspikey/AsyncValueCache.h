#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

// Slow device queries never run on the HTTP thread. A worker retains its state
// independently of the cache object's lifetime; at most one query per key runs.
class AsyncValueCache {
    using Clock = std::chrono::steady_clock;
    struct Entry { int value = -1; bool pending = false; Clock::time_point checked{}; };
    struct State {
        std::mutex mutex;
        std::map<std::string, Entry> entries;
        std::function<bool(const std::string&, int&)> read;
        std::chrono::seconds interval;
    };
    std::shared_ptr<State> state;
public:
    explicit AsyncValueCache(std::function<bool(const std::string&, int&)> reader,
                             std::chrono::seconds interval = std::chrono::seconds(60))
        : state(std::make_shared<State>()) { state->read = std::move(reader); state->interval = interval; }
    bool Get(const std::string& key, int& value) {
        auto retained = state;
        std::lock_guard<std::mutex> lock(retained->mutex);
        auto& entry = retained->entries[key];
        bool known = entry.value >= 0;
        if (known) value = entry.value;
        if (!entry.pending && (entry.checked == Clock::time_point{} || Clock::now() - entry.checked >= retained->interval)) {
            entry.pending = true;
            try {
                std::thread([retained, key] {
                    int fetched = -1;
                    bool valid = false;
                    try { valid = retained->read(key, fetched); } catch (...) {}
                    std::lock_guard<std::mutex> complete(retained->mutex);
                    auto& result = retained->entries[key];
                    if (valid && fetched >= 0 && fetched <= 100) result.value = fetched;
                    result.pending = false;
                    result.checked = Clock::now();
                }).detach();
            } catch (...) {
                entry.pending = false;
                entry.checked = Clock::now();
            }
        }
        return known;
    }
};
