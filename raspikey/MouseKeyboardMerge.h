#pragma once
#include <array>
#include <vector>
#include <algorithm>
inline std::array<uint8_t,9> MergeMouseKeyboard(const std::vector<std::array<uint8_t,9>>& inputs) {
    std::array<uint8_t,9> result{{2}};size_t slot=3;
    for(const auto& report:inputs)result[1]|=report[1];
    for(const auto& report:inputs) {
        for(size_t i=3;i<report.size();++i)if(report[i]) {
            if(report[i]<=3){std::fill(result.begin()+3,result.end(),1);return result;}
            if(std::find(result.begin()+3,result.begin()+slot,report[i])!=result.begin()+slot)continue;
            if(slot==result.size()){std::fill(result.begin()+3,result.end(),1);return result;}
            result[slot++]=report[i];
        }
    }
    return result;
}
