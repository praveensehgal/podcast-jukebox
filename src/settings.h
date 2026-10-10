#pragma once
#include <Arduino.h>

// Persistent user settings (NVS namespace "set"). Small on purpose.
namespace settings {

enum class Output : uint8_t { Speaker = 0, Bluetooth = 1 };

Output output();
void setOutput(Output o);

uint8_t volume();          // 0..16
void setVolume(uint8_t v);

uint16_t sleepMinutes();   // 0 = off; 30, 60, 90
void setSleepMinutes(uint16_t m);

const char* sleepLabel();  // "Off" / "30 min" ...

}  // namespace settings
