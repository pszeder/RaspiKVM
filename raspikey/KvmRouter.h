#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
void StartKvm();
void StopKvm();
int KvmForward(bool mouse,const uint8_t* report,size_t size);
void KvmRelease(bool mouse);
std::string GetKvmStatus();
std::string UpdateKvm(const std::string& body);
bool KvmBluetoothEnabled();
bool KvmPairingOpen();
bool KvmAcceptHost(const std::string& address);
std::string KvmHostAddress();
void KvmHostSeen(const std::string& address,const std::string& name,bool connected,bool paired);
void KvmBluetoothStatus(bool registered,bool ready,const std::string& error);
int KvmLedFd();
bool KvmReadLed(uint8_t* report);
void KvmHostLed(bool bluetooth,uint8_t value);
