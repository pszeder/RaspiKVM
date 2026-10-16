#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
void StartKvmBluetooth();
void StopKvmBluetooth();
bool SendKvmBluetooth(uint8_t report,const uint8_t* data,size_t size);
void ForgetKvmBluetoothHost(const char* address);
