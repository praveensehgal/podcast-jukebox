#pragma once
#include <Arduino.h>

#include <vector>

#include "../channels.h"
#include "../input.h"

// Screen set from the mockup. Each screen owns its LVGL objects; `show` builds it
// on a fresh lv screen and loads it, `handle` consumes one input event and returns
// true if it consumed it. The app state machine (app.cpp) decides transitions.
namespace ui {

// Boot: title, version, and a rolling status log while Wi-Fi / OTA / channels run.
void bootShow(const char* version);
void bootStatus(const char* line);

// Screen 01: channel list.
void channelsShow(const std::vector<channels::Channel>& list, int selected);
bool channelsHandle(const input::Event& e);
int channelsSelected();

// Transient volume bar, drawn over whichever screen is active. Hides after 2 s.
void volumeFlash(int level);  // 0..16
void volumeTick();

}  // namespace ui
