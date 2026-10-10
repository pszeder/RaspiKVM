#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>

namespace Hidpp {
constexpr uint8_t SoftwareId=0x0e;
inline std::array<uint8_t,20> Request(uint8_t feature, uint8_t function, const std::vector<uint8_t>& parameters) {
    if(function>15 || parameters.size()>16)throw std::invalid_argument("Invalid HID++ request");
    std::array<uint8_t,20> result{{0x11,0xff,feature,uint8_t((function<<4)|SoftwareId)}};
    for(size_t i=0;i<parameters.size();++i)result[4+i]=parameters[i];
    return result;
}
inline bool Packet(const uint8_t* p,size_t n) {return ((n==20&&p[0]==0x11)||(n==7&&p[0]==0x10))&&(p[1]==0||p[1]==0xff);}
inline bool Reply(const uint8_t* p,size_t n,uint8_t feature,uint8_t function) {
    return Packet(p,n)&&p[2]==feature&&p[3]==uint8_t((function<<4)|SoftwareId);
}
inline bool Error(const uint8_t* p,size_t n,uint8_t feature,uint8_t function) {
    return Packet(p,n)&&p[2]==0xff&&p[3]==feature&&p[4]==uint8_t((function<<4)|SoftwareId);
}
inline bool Toggle(const uint8_t* p,size_t n,uint8_t feature,bool& pressed) {
    if(!feature||!Packet(p,n)||p[2]!=feature||p[3]!=0||n<12)return false;
    pressed=false;
    // divertedButtonsEvent contains up to four big-endian control IDs.
    for(size_t i=4;i<12;i+=2)if(p[i]==0&&p[i+1]==0xc4)pressed=true;
    return true;
}
}
