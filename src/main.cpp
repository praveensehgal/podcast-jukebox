// Podcast Jukebox — firmware entry.
//
// Boot: screen on -> Wi-Fi (captive portal on first run) -> update check -> channel
// list -> UI. The loop pumps LVGL and routes knob/button events (or their serial
// stand-ins) to the active screen.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_random.h>

#include "app.h"
#include "audio_out.h"
#include "bt_emitter.h"
#include "channels.h"
#include "display.h"
#include "input.h"
#include "ota.h"
#include "ui/ui.h"
#include "version.h"

static const char* kSetupSsid = "Jukebox-Setup";
static const uint32_t kPortalTimeoutS = 180;

static std::vector<channels::Channel> channelList;

// Per-device key for the setup hotspot. WiFiManager registers its /update
// firmware-upload route unconditionally, so the hotspot must not be open. The key is
// random, generated once from the hardware RNG and kept in NVS; it is never derived
// from anything the AP broadcasts. It is printed on serial and shown on the boot
// screen so nobody ever types it from memory.
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
        ui::bootStatus("No Wi-Fi yet. On a phone, join:");
        ui::bootStatus((String("  network  ") + kSetupSsid).c_str());
        ui::bootStatus((String("  password ") + key).c_str());
        ui::bootStatus("then open 192.168.4.1");
    });
    ui::bootStatus("Connecting to Wi-Fi...");
    bool ok = wm.autoConnect(kSetupSsid, key.c_str());
    if (ok) {
        Serial.printf("[net] %s  ip %s  rssi %d\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        ui::bootStatus((String("Wi-Fi: ") + WiFi.SSID()).c_str());
    } else {
        Serial.println("[net] no Wi-Fi; portal timed out");
        ui::bootStatus("No Wi-Fi. Will keep trying after restart.");
    }
    return ok;
}

static void loadChannels() {
    bool cached = false;
    channelList = channels::load(cached);
    Serial.printf("[channels] %u channels%s\n", (unsigned)channelList.size(), cached ? " (from cache)" : "");
    for (const auto& c : channelList) Serial.printf("  %2u  %-24s %s\n", c.position, c.name.c_str(), c.feed.c_str());
    char line[48];
    snprintf(line, sizeof(line), "%u channels%s", (unsigned)channelList.size(), cached ? " (saved copy)" : "");
    ui::bootStatus(line);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n%s %s  (built %s %s)\n", FW_NAME, fwVersion(), __DATE__, __TIME__);

    display::begin();
    ui::bootShow(fwVersion());
    input::begin();

    if (connectWifi()) {
        ui::bootStatus("Checking for updates...");
        ota::Result r = ota::checkAndUpdate();
        Serial.printf("[ota] %s\n", ota::resultName(r));
        ui::bootStatus(r == ota::Result::UpToDate ? "Up to date" : ota::resultName(r));
    }
    loadChannels();
    audio_out::begin();
    bt::begin();
    app::begin(&channelList, setupKey());
    Serial.println("[boot] ready");
}

void loop() {
    input::Event e;
    while (input::poll(e)) app::handle(e);
    ui::volumeTick();
    audio_out::loop();
    app::loop();
    uint32_t wait = display::tick();
    delay(min<uint32_t>(wait, 5));
}
