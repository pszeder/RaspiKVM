#pragma once
#include "MouseControl.h"
#include "json.hpp"
#include <array>
#include <stdexcept>

struct MouseConfiguration {
    std::string scrollMode="free";
    unsigned smartShift=20;
    std::array<MouseButtonAction,5> buttons{};
    MouseConfiguration(){buttons[4].action=1;}
};
inline const std::array<const char*,5>& MouseButtonNames() {
    static const std::array<const char*,5> names{{"thumb","middle","back","forward","scrollToggle"}};
    return names;
}
inline MouseConfiguration ParseMouseConfiguration(const std::string& body) {
    auto j=nlohmann::json::parse(body);MouseConfiguration result;
    if(!j.is_object()||!j.at("scrollMode").is_string())throw std::invalid_argument("Choose a scroll mode.");
    result.scrollMode=j.at("scrollMode").get<std::string>();
    if(result.scrollMode!="free"&&result.scrollMode!="ratchet"&&result.scrollMode!="smart")throw std::invalid_argument("Unknown scroll mode.");
    if(!j.at("smartShift").is_number_integer())throw std::invalid_argument("SmartShift speed must be a whole number from 2 to 49.");
    int64_t speed=j.at("smartShift").get<int64_t>();if(speed<2||speed>49)throw std::invalid_argument("SmartShift speed must be from 2 to 49.");result.smartShift=unsigned(speed);
    auto actions=j.at("buttons");if(!actions.is_object()||actions.size()!=5)throw std::invalid_argument("Supply all five button actions.");
    for(size_t i=0;i<5;++i) {
        auto action=actions.at(MouseButtonNames()[i]);auto kind=action.at("action").get<std::string>();
        if(kind!="default"&&kind!="shortcut"&&kind!="disabled")throw std::invalid_argument("Unknown button action.");
        if(!action.at("modifiers").is_number_integer()||!action.at("key").is_number_integer())throw std::invalid_argument("Choose a valid key combination.");
        int64_t modifiers=action.at("modifiers").get<int64_t>(),key=action.at("key").get<int64_t>();
        if(modifiers<0||modifiers>255||key<4||key>101)throw std::invalid_argument("Unsupported key combination.");
        result.buttons[i]={uint8_t(kind=="shortcut"?1:kind=="disabled"?2:0),uint8_t(modifiers),uint8_t(key)};
    }
    return result;
}
inline nlohmann::json MouseConfigurationJson(const MouseConfiguration& config) {
    nlohmann::json buttons=nlohmann::json::object();
    for(size_t i=0;i<5;++i) {auto b=config.buttons[i];buttons[MouseButtonNames()[i]]={{"action",b.action==1?"shortcut":b.action==2?"disabled":"default"},{"modifiers",b.modifiers},{"key",b.key}};}
    return {{"scrollMode",config.scrollMode},{"smartShift",config.smartShift},{"buttons",buttons}};
}
