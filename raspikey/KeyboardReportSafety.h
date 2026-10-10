#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>

// Only an aggregate error count is retained; no key usages are recorded.
inline std::atomic<uint32_t>& KeyboardErrorReports()
{
    static std::atomic<uint32_t> count{0};
    return count;
}

inline bool KeyboardKeysValid(const uint8_t* keys, size_t count)
{
    for(size_t i=0;i<count;++i) {
        // HID keyboard ErrorRollOver, POSTFail and ErrorUndefined are not keys.
        if(keys[i]>=1 && keys[i]<=3) {
            KeyboardErrorReports().fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
    return true;
}
