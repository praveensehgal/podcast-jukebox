// Podcast Jukebox — boot skeleton.
//
// What this firmware does today (v0.x): join Wi-Fi (captive portal on first run),
// check for a newer release, update itself, then idle. Screen, knobs, audio and the
// podcast resolver land in later releases; the point of this skeleton is that every
// device shipped from now on can already be updated remotely.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "channels.h"
#include "ota.h"
#include "version.h"

static const char* kSetupSsid = "Jukebox-Setup";
static const uint32_t kPortalTimeoutS = 180;

// Per-device key for the setup hotspot: "jb-" + last three MAC bytes. WiFiManager
// registers its /update firmware-upload route unconditionally, so the hotspot must
// not be open. The key is printed on serial and, once the screen exists, embedded in
// the Wi-Fi screen's QR code, so nobody ever types it.
static String setupKey() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char key[12];
    snprintf(key, sizeof(key), "jb-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(key);
}

static bool connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("podcast-jukebox");
    WiFiManager wm;
    wm.setConfigPortalTimeout(kPortalTimeoutS);
    wm.setConnectTimeout(20);
    std::vector<const char*> menu = {"wifi", "info", "exit"};  // no upload page in the menu
    wm.setMenu(menu);
    String key = setupKey();
    wm.setAPCallback([&key](WiFiManager*) {
        Serial.printf("[net] setup hotspot %s  key %s  (open http://192.168.4.1)\n", kSetupSsid, key.c_str());
    });
    // Uses credentials saved in NVS; opens the WPA2 "Jukebox-Setup" portal only if they fail.
    bool ok = wm.autoConnect(kSetupSsid, key.c_str());
    if (ok) {
        Serial.printf("[net] %s  ip %s  rssi %d\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("[net] no Wi-Fi; portal timed out");
    }
    return ok;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n%s %s  (built %s %s)\n", FW_NAME, fwVersion(), __DATE__, __TIME__);

    if (connectWifi()) {
        ota::Result r = ota::checkAndUpdate();
        Serial.printf("[ota] %s\n", ota::resultName(r));
    }

    bool cached = false;
    std::vector<channels::Channel> list = channels::load(cached);
    Serial.printf("[channels] %u channels%s\n", (unsigned)list.size(), cached ? " (from cache)" : "");
    for (const auto& c : list) Serial.printf("  %2u  %-24s %s\n", c.position, c.name.c_str(), c.feed.c_str());

    Serial.println("[boot] ready");
}

void loop() {
    // Nothing yet. Later: LVGL tick, encoder/button scan, audio pump.
    delay(1000);
}
