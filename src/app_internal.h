#pragma once
// Shared between app.cpp (browse + playback) and app_setup.cpp (Settings, Wi-Fi,
// Bluetooth, phone setup). Not part of the public app interface.
#include <Arduino.h>

#include <vector>

#include "channels.h"

namespace app {

enum class Screen { Channels, Episodes, Playing, Settings, Wifi, Password, Bluetooth, Phone };

extern Screen screen;
extern std::vector<channels::Channel>* chans;

void showChannels();
void stopPlayback();   // save the place and stop; PLAY can pick it back up

namespace setup {
void begin(const String& hotspotKey);
void openSettings(int row);
void pick(int index);
void back();
void join();
void cancel();         // close the phone-setup hotspot if it is up
void loop();
}  // namespace setup

}  // namespace app
