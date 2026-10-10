#pragma once
#include <Arduino.h>

#include <vector>

#include "channels.h"
#include "input.h"

// The state machine behind the three main screens (decision 001):
//   Channels --OK--> Episodes --OK--> Playing
//   STOP from anywhere: stop audio, save position, back to Channels.
//   PAUSE from anywhere: toggle the current episode.
//   OK on Playing: back to this channel's episode list, audio keeps going.
namespace app {

// `hotspotKey` is shown in the Wi-Fi screen's QR code.
void begin(std::vector<channels::Channel>* list, const String& hotspotKey);
void handle(const input::Event& e);
void loop();  // position reporting, Now Playing refresh, end-of-episode handling

}  // namespace app
