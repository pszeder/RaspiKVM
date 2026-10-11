#pragma once
// Power-state events cross between the output transport and report router.
bool WakeKvmBluetoothHost();
bool RequestKvmBluetoothConnection();
void PauseKvmBluetoothAdvertising();
bool KvmBluetoothSelected();
void KvmBluetoothSleeping(bool sleeping);
