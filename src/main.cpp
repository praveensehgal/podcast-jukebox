// Podcast Jukebox — boot skeleton.
//
// What this firmware does today (v0.x): join Wi-Fi (captive portal on first run),
// check for a newer release, update itself, then idle. Screen, knobs, audio and the
// podcast resolver land in later releases; the point of this skeleton is that every
// device shipped from now on can already be updated remotely.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_random.h>

#include "channels.h"
#include "ota.h"
#include "version.h"

static const char* kSetupSsid = "Jukebox-Setup";
static const uint32_t kPortalTimeoutS = 180;

// Per-device key for the setup hotspot. WiFiManager registers its /update
// firmware-upload route unconditionally, so the hotspot must not be open. The key is
// random, generated once from the hardware RNG and kept in NVS; it is never derived
// from anything the AP broadcasts. It is printed on serial and, once the screen
// exists, embedded in the Wi-Fi screen's QR code, so nobody ever types it.
static String setupKey() {
    Preferences p;
    p.begin("net", false);
    String key = p.getString("setupkey", "");
    if (key.length() < 12) {
        static const char* alphabet = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/O/1/l/i
        const size_t n = strlen(alphabet);
        char buf[13];
        for (int i = 0; i < 12; i++) buf[i] = alphabet[esp_random() % n];
        buf[12] = '\0';
        key = String(buf);
        p.putString("setupkey", key);
    }
    p.end();
    return key;
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
