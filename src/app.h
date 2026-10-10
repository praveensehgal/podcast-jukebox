#pragma once
#include <Arduino.h>

#include <vector>

#include "channels.h"
#include "input.h"

// Touch navigation (decision 004) plus the three top buttons:
//   Channels --tap--> Episodes --tap--> Now Playing;  Back in the header goes up one level.
//   PLAY resumes the last episode, PAUSE toggles, STOP saves the place and returns to
//   Channels from anywhere, holding STOP for 3 s opens Settings.
namespace app {

// `hotspotKey` is the setup hotspot's WPA2 key, shown on the phone-setup screen.
void begin(std::vector<channels::Channel>* list, const String& hotspotKey);
void handle(const input::Event& e);
void loop();   // position saves, Now Playing refresh, end of episode, sleep timer

}  // namespace app
