#pragma once
#include <atomic>
#include <cstdint>
#include <string>

// Button order: thumb, middle, back, forward, scroll toggle.
struct MouseButtonAction {uint8_t action=0, modifiers=0, key=70;}; // 0 original, 1 shortcut, 2 disabled
struct MouseShortcutEvent {uint8_t source=4, modifiers=0, key=0; bool pressed=false;};
MouseButtonAction GetMouseButtonAction(const std::string& address, unsigned button);
std::string GetMouseControlSettings(const std::string& address);
std::string SetMouseControlSettings(const std::string& address, const std::string& body);
void RunMouseControl(const std::atomic<bool>* stopping);
int MouseControlEventFd();
bool TakeMouseShortcutEvent(MouseShortcutEvent& event);
