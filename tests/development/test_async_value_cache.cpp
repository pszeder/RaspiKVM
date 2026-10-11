#include "../../raspikey/AsyncValueCache.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <iostream>

int main() {
    struct Reader {
        std::mutex mutex;
        std::condition_variable ready;
        bool blocked = true;
        int value = 73;
        bool valid = true;
        std::atomic<int> calls{0};
    };
    auto reader = std::make_shared<Reader>();
    AsyncValueCache cache([reader](const std::string&, int& value) {
        ++reader->calls;
        std::unique_lock<std::mutex> lock(reader->mutex);
        reader->ready.wait(lock, [&] { return !reader->blocked; });
        value = reader->value;
        return reader->valid;
    }, std::chrono::seconds(0));
    auto await = [](std::function<bool()> condition) {
        for (int i = 0; i < 2000; ++i) {
            if (condition()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(false && "background query timed out");
    };
    int value = -1;
    auto start = std::chrono::steady_clock::now();
    assert(!cache.Get("keyboard", value));
    await([&] { return reader->calls.load() == 1; });
    for (int i = 0; i < 100; ++i) assert(!cache.Get("keyboard", value));
    assert(reader->calls == 1);
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
    { std::lock_guard<std::mutex> lock(reader->mutex); reader->blocked = false; }
    reader->ready.notify_all();
    await([&] { return cache.Get("keyboard", value); });
    assert(value == 73);
    { std::lock_guard<std::mutex> lock(reader->mutex); reader->blocked = true; reader->valid = false; }
    int before = reader->calls;
    await([&] { cache.Get("keyboard", value); return reader->calls > before; });
    assert(value == 73);
    { std::lock_guard<std::mutex> lock(reader->mutex); reader->blocked = false; }
    reader->ready.notify_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(cache.Get("keyboard", value) && value == 73);
    { std::lock_guard<std::mutex> lock(reader->mutex); reader->valid = true; reader->value = 0; }
    await([&] { return cache.Get("keyboard", value) && value == 0; });
    { std::lock_guard<std::mutex> lock(reader->mutex); reader->value = 101; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(cache.Get("keyboard", value) && value == 0);
    std::cout << "Async cache: blocked reads, deduplication, failed reads, zero and range checks passed\n";
}
