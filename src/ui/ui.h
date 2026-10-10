#pragma once
#include <Arduino.h>

#include <vector>

#include "../channels.h"
#include "../resolver_client.h"

// The eight touch screens from docs/screens/podcast-jukebox-touch-screens.html, plus the
// boot log and a notice. Each `...Show` builds a fresh LVGL screen and loads it. Taps
// come back to the app as input events (see widgets.h); nothing here calls the app.
namespace ui {

// Boot: title, version, rolling status lines while Wi-Fi / updates / channels load.
void bootShow(const char* version);
void bootStatus(const char* line);

// Full-screen two-line notice (fetching, starting, errors).
void noticeShow(const char* line1, const char* line2);

// Initials + tile colour for a channel name (list tiles and Now Playing art).
void channelArt(const char* name, char* initialsOut, uint32_t& colorOut);

// 01 Channels. Taps: Pick(index), Gear, NowPlaying.
void channelsShow(const std::vector<channels::Channel>& list, int page, int playingIndex, bool playing);
int channelsPage();

// 02 Episodes. Taps: Pick(index), Back, NowPlaying.
void episodesShow(const char* channelName, const std::vector<resolver::Episode>& list, int page,
                  const String& playingId, bool playing);
int episodesPage();

// 03 Now Playing. Taps: Back, SkipBack, Pause, Stop, SkipFwd, VolDown, VolUp.
void playingShow(const char* channelName, const char* initials, uint32_t artColor, const resolver::Episode& e);
void playingUpdate(uint32_t posS, uint32_t durS, bool paused, uint8_t volume16);

// 04 Settings. Taps: Pick(row), Back.
struct SettingsView {
    String wifi;          // SSID or "Not connected"
    bool wifiOk;
    String bluetooth;     // "Off", "Connected: X", "Not fitted"
    uint32_t btDot;       // 0 = none
    bool soundBluetooth;
    const char* sleep;
    const char* version;
};
enum SettingsRow { kRowWifi, kRowBluetooth, kRowSound, kRowSleep, kRowRefresh, kRowAbout, kSettingsRows };
void settingsShow(const SettingsView& v, int page);

// 05 Wi-Fi. Row 0 is "Set up with a phone", networks follow. Taps: Pick(row), Back.
struct Net {
    String ssid;
    int rssi;
    bool open;
};
void wifiShow(const std::vector<Net>& nets, bool scanning);

// 06 Password keyboard. Taps: Join, Back.
void passwordShow(const char* ssid);
const String& passwordText();
void passwordTick();

// 07 Bluetooth. Row 0 is the hint, devices follow. Taps: Pick(row), Back.
void bluetoothShow(bool fitted, bool scanning);
void bluetoothRefresh(bool scanning);

// 08 Set up with a phone (QR for the setup hotspot). Taps: Back.
void phoneShow(const char* ssid, const char* key);

}  // namespace ui
