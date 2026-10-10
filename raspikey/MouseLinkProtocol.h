#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace MouseLinkProtocol {
inline uint16_t U16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline void Put16(uint8_t* p,uint16_t value){p[0]=uint8_t(value);p[1]=uint8_t(value>>8);}
inline bool ControllerAddress(const uint8_t* info,size_t length,std::array<uint8_t,6>& address) {
    // hci_dev_info begins with dev_id (2), name (8), then bdaddr (6).
    if(length<16)return false;
    for(size_t i=0;i<6;++i)address[i]=info[10+i];
    return true;
}
inline bool Address(const std::string& text,std::array<uint8_t,6>& bytes) {
    if(text.size()!=17)return false;
    auto hex=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(size_t i=0;i<6;++i) {
        int hi=hex(text[i*3]),lo=hex(text[i*3+1]);
        if(hi<0||lo<0||(i<5&&text[i*3+2]!=':'))return false;
        bytes[5-i]=uint8_t((hi<<4)|lo);
    }
    return true;
}
inline std::array<uint8_t,23> Request(uint16_t index,const std::array<uint8_t,6>& address,uint8_t type) {
    // MGMT Load Connection Parameters, exactly one existing device. This avoids
    // replacing other devices' parameters and lets Linux update the live link.
    std::array<uint8_t,23> result{};
    Put16(result.data(),0x0035);Put16(result.data()+2,index);Put16(result.data()+4,17);
    Put16(result.data()+6,1);
    for(size_t i=0;i<6;++i)result[8+i]=address[i];
    result[14]=type;Put16(result.data()+15,6);Put16(result.data()+17,6);
    Put16(result.data()+19,0);Put16(result.data()+21,216);
    return result;
}
struct Update {uint16_t interval=0,latency=0,timeout=0;uint8_t status=0;};
inline bool CommandResult(const uint8_t* packet,size_t length,uint16_t controller,uint8_t& status) {
    if(length<9||U16(packet+2)!=controller||U16(packet+4)<3||size_t(U16(packet+4))+6>length)return false;
    auto event=U16(packet);
    if(event==1&&U16(packet+6)==0x0035){status=packet[8];return true;}
    if(event==2&&U16(packet+7)==0x0035){status=packet[6];return true;}
    return false;
}
inline bool ConnectionUpdate(const uint8_t* packet,size_t length,uint16_t handle,Update& update) {
    // Raw HCI event packet, LE Connection Update Complete. No ACL/HID data.
    if(length<13||packet[0]!=4||packet[1]!=0x3e||packet[2]<10||packet[3]!=3||
       size_t(packet[2])+3>length||U16(packet+5)!=handle)return false;
    update.status=packet[4];update.interval=U16(packet+7);
    update.latency=U16(packet+9);update.timeout=U16(packet+11);return true;
}
}
