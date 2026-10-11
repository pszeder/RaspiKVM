#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
void StartTvControl();
void StopTvControl();
void TvHostChanged(bool bluetooth,bool explicitSwitch);
void TvInputActivity(bool mouse,const uint8_t* report,size_t size);
std::string GetTvStatus();
void UpdateTv(const std::string& body);
