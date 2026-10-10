#pragma once
#include <Arduino.h>

#include <vector>

#include "../channels.h"
#include "../input.h"
#include "../resolver_client.h"

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
// Initials + colour for a channel name, shared by the list tiles and Now Playing art.
void channelArt(const char* name, char* initialsOut, uint32_t& colorOut);

// Screen 02: episode list for one channel.
void episodesShow(const char* channelName, const std::vector<resolver::Episode>& list, int selected);
bool episodesHandle(const input::Event& e);
int episodesSelected();

// Screen 03: Now Playing. Update once a second from the app loop.
void playingShow(const char* channelName, const char* initials, uint32_t artColor, const char* episodeTitle);
void playingUpdate(uint32_t posS, uint32_t durS, bool paused);

// Short full-screen notice (fetching, errors). Any input returns to the caller.
void noticeShow(const char* line1, const char* line2);

// Screen 04: Settings.
void settingsShow(int selected);
void settingsRefresh();
bool settingsHandle(const input::Event& e);
int settingsSelected();   // 0 Wi-Fi, 1 Bluetooth, 2 Sound output, 3 Sleep, 4 Check, 5 About, 6 Back

// Screen 05: Wi-Fi list + QR. Call wifiScan() (blocking, ~3 s) before wifiShow().
void wifiScan();
void wifiShow(const char* setupSsid, const char* setupKey);
bool wifiHandle(const input::Event& e);
String wifiSelected();

// Screen 06: password strip.
void passwordShow(const char* ssid);
int passwordHandle(const input::Event& e);   // 0 handled, 1 DONE, -1 not handled
void passwordTick();
const String& passwordText();

// Screen 07: Bluetooth.
void bluetoothShow();
void bluetoothRefresh();
bool bluetoothHandle(const input::Event& e);
int bluetoothAction();    // device index >= 0, -1 output toggle, -2 forget, -3 back, -4 none

// Transient volume bar, drawn over whichever screen is active. Hides after 2 s.
void volumeFlash(int level);  // 0..16
void volumeTick();

}  // namespace ui
