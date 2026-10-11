#pragma once
#include <array>
#include <algorithm>
#include <cstdint>

// Only aggregate timing is retained: never positions, keys or button values.
struct MouseTiming {
    uint64_t frames=0, gaps=0, gapSum=0, queueSum=0, readSum=0, writeSum=0;
    uint64_t maxQueue=0, maxWrite=0, lastStamp=0;
    std::array<uint64_t,101> gapHistogram{}; // 500 us buckets, idle gaps excluded.
    std::array<uint64_t,101> queueHistogram{}; // 100 us buckets, last is overflow.
    void Frame(uint64_t stamp,uint64_t before,uint64_t after) {
        if(before<stamp || after<before)return;
        uint64_t queue=after-stamp, write=after-before;
        ++frames; queueSum+=queue; readSum+=before-stamp; writeSum+=write;
        maxQueue=std::max(maxQueue,queue);maxWrite=std::max(maxWrite,write);
        ++queueHistogram[std::min<uint64_t>(100,queue/100)];
        if(lastStamp&&stamp>lastStamp&&stamp-lastStamp<=50000) {
            uint64_t gap=stamp-lastStamp;++gaps;gapSum+=gap;
            ++gapHistogram[std::min<uint64_t>(100,gap/500)];
        }
        lastStamp=stamp;
    }
    static uint64_t Percentile(const std::array<uint64_t,101>& hist,uint64_t count,unsigned percent,uint64_t width) {
        if(!count)return 0;
        uint64_t target=(count*percent+99)/100,seen=0;
        for(size_t i=0;i<hist.size();++i)if((seen+=hist[i])>=target)return i==hist.size()-1 ? 0 : (i+1)*width;
        return 0;
    }
};
