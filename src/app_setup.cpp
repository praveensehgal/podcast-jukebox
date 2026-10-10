// Settings, Wi-Fi (list, password keyboard, phone setup) and Bluetooth screens.

#include <WiFi.h>
#include <WiFiManager.h>

#include <algorithm>

#include "app_internal.h"
#include "bt_emitter.h"
#include "resolver_client.h"
#include "settings.h"
#include "ui/ui.h"
#include "version.h"

namespace app {
namespace setup {

static const char* kSetupSsid = "Jukebox-Setup";
static const uint32_t kPortalTimeoutS = 300;
static const uint32_t kJoinTimeoutMs = 20000;
static const size_t kMaxNets = 15;

static String hotspotKey;
static std::vector<ui::Net> nets;
static String pendingSsid;
static WiFiManager* portal = nullptr;   // only while the phone-setup screen is up
static bool btWasScanning = false;

void begin(const String& key) { hotspotKey = key; }

// ---------- 04 Settings ----------

static ui::SettingsView view() {
    ui::SettingsView v;
    v.wifiOk = WiFi.status() == WL_CONNECTED;
    v.wifi = v.wifiOk ? WiFi.SSID() : String("Not connected");
    String bs = bt::status();
    v.bluetooth = bt::fitted() ? bs : String("Not fitted");
    v.btDot = bs.startsWith("Connected") ? 0x2F7D4F : 0;
    v.soundBluetooth = settings::output() == settings::Output::Bluetooth;
    v.sleep = settings::sleepLabel();
    v.version = fwVersion();
    return v;
}

void openSettings(int row) {
    screen = Screen::Settings;
    ui::settingsShow(view(), row / 4);
}

static void pause(const char* a, const char* b, uint32_t ms) {
    ui::noticeShow(a, b);
    delay(ms);
}

// ---------- 05 Wi-Fi ----------

static void scan() {
    ui::noticeShow("Looking for networks...", nullptr);
    int n = WiFi.scanNetworks();
    nets.clear();
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;
        auto same = std::find_if(nets.begin(), nets.end(), [&](const ui::Net& x) { return x.ssid == ssid; });
        if (same != nets.end()) { same->rssi = std::max(same->rssi, (int)WiFi.RSSI(i)); continue; }
        nets.push_back({ssid, (int)WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
    }
    WiFi.scanDelete();
    std::sort(nets.begin(), nets.end(), [](const ui::Net& a, const ui::Net& b) { return a.rssi > b.rssi; });
    if (nets.size() > kMaxNets) nets.resize(kMaxNets);
    Serial.printf("[net] scan: %u networks\n", (unsigned)nets.size());
}

static void showWifi(bool rescan) {
    stopPlayback();   // scanning and joining stall the stream anyway
    if (rescan || nets.empty()) scan();
    screen = Screen::Wifi;
    ui::wifiShow(nets, false);
}

static bool waitConnected() {
    uint32_t until = millis() + kJoinTimeoutMs;
    while (WiFi.status() != WL_CONNECTED && millis() < until) delay(200);
    return WiFi.status() == WL_CONNECTED;
}

// Try the new network without saving it, so a mistyped password cannot strand the
// device: on failure it goes back to the network it had. Saved only once it works.
static void connectTo(const String& ssid, const String& pass) {
    String prevSsid = WiFi.SSID(), prevPass = WiFi.psk();
    ui::noticeShow(ssid.c_str(), "Connecting...");
    WiFi.persistent(false);
    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), pass.c_str());
    bool ok = waitConnected();
    if (ok) {
        WiFi.persistent(true);   // reconnect once more with saving on, so boot finds it
        WiFi.disconnect(false, false);
        WiFi.begin(ssid.c_str(), pass.c_str());
        ok = waitConnected();
    } else if (prevSsid.length()) {
        WiFi.begin(prevSsid.c_str(), prevPass.c_str());
    }
    WiFi.persistent(true);
    Serial.printf("[net] join %s: %s\n", ssid.c_str(), ok ? "ok" : "failed");
    if (ok) { pause("Connected", ssid.c_str(), 1500); openSettings(ui::kRowWifi); return; }
    pause("Could not connect.", "Check the password and try again.", 2500);
    screen = Screen::Password;
    ui::passwordShow(ssid.c_str());
}

void join() {
    if (screen == Screen::Password) connectTo(pendingSsid, ui::passwordText());
}

// ---------- 08 Set up with a phone ----------

static void openPhone() {
    stopPlayback();
    screen = Screen::Phone;
    ui::phoneShow(kSetupSsid, hotspotKey.c_str());
    portal = new WiFiManager();
    portal->setConfigPortalBlocking(false);
    portal->setConfigPortalTimeout(kPortalTimeoutS);
    std::vector<const char*> menu = {"wifi", "exit"};   // no firmware-upload page
    portal->setMenu(menu);
    portal->startConfigPortal(kSetupSsid, hotspotKey.c_str());
    Serial.printf("[net] phone setup: hotspot %s up\n", kSetupSsid);
}

void cancel() {
    if (!portal) return;
    portal->stopConfigPortal();
    delete portal;
    portal = nullptr;
    WiFi.mode(WIFI_STA);
    WiFi.begin();   // back onto the saved network
}

static void portalLoop() {
    if (!portal) return;
    if (portal->process()) {   // phone saved a network and the device joined it
        delete portal;
        portal = nullptr;
        WiFi.mode(WIFI_STA);
        pause("Connected", WiFi.SSID().c_str(), 1500);
        openSettings(ui::kRowWifi);
    } else if (!portal->getConfigPortalActive()) {   // timed out
        cancel();
        showWifi(false);
    }
}

// ---------- 07 Bluetooth ----------

static void showBluetooth() {
    screen = Screen::Bluetooth;
    if (bt::fitted() && !bt::scanning() && bt::devices().empty()) bt::startScan();
    btWasScanning = bt::scanning();
    ui::bluetoothShow(bt::fitted(), btWasScanning);
}

static void bluetoothPick(int i) {
    if (!bt::fitted()) return;
    if (i == 0) { bt::startScan(); showBluetooth(); return; }
    const auto& devs = bt::devices();
    if (i - 1 >= (int)devs.size()) return;
    ui::noticeShow(devs[i - 1].name.c_str(), "Connecting...");
    if (bt::connect(devs[i - 1])) settings::setOutput(settings::Output::Bluetooth);
    showBluetooth();
}

// ---------- events ----------

static void settingsPick(int row) {
    switch (row) {
        case ui::kRowWifi: showWifi(true); return;
        case ui::kRowBluetooth: showBluetooth(); return;
        case ui::kRowSound:
            settings::setOutput(settings::output() == settings::Output::Speaker ? settings::Output::Bluetooth
                                                                                 : settings::Output::Speaker);
            break;
        case ui::kRowSleep: {
            uint16_t m = settings::sleepMinutes();
            settings::setSleepMinutes(m == 0 ? 30 : m == 30 ? 60 : m == 60 ? 90 : 0);
            break;
        }
        case ui::kRowRefresh: {
            bool cached = false;
            ui::noticeShow("Checking for new episodes...", nullptr);
            *chans = channels::load(cached);
            pause("Channel list refreshed", cached ? "(saved copy, no network)" : nullptr, 1200);
            break;
        }
        case ui::kRowAbout:
            pause((String("Podcast Jukebox v") + fwVersion()).c_str(), (String("Device ") + resolver::deviceId()).c_str(), 3000);
            break;
        default: return;
    }
    openSettings(row);
}

void pick(int i) {
    switch (screen) {
        case Screen::Settings: settingsPick(i); break;
        case Screen::Bluetooth: bluetoothPick(i); break;
        case Screen::Wifi:
            if (i == 0) { openPhone(); break; }
            if (i - 1 >= (int)nets.size()) break;
            pendingSsid = nets[i - 1].ssid;
            if (nets[i - 1].open) { connectTo(pendingSsid, ""); break; }
            screen = Screen::Password;
            ui::passwordShow(pendingSsid.c_str());
            break;
        default: break;
    }
}

void back() {
    switch (screen) {
        case Screen::Settings: showChannels(); break;
        case Screen::Wifi: openSettings(ui::kRowWifi); break;
        case Screen::Password: screen = Screen::Wifi; ui::wifiShow(nets, false); break;
        case Screen::Bluetooth: openSettings(ui::kRowBluetooth); break;
        case Screen::Phone: cancel(); showWifi(false); break;
        default: showChannels(); break;
    }
}

void loop() {
    ui::passwordTick();
    bt::loop();
    portalLoop();
    if (screen != Screen::Bluetooth) return;
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();
    if (bt::scanning() != btWasScanning) showBluetooth();   // header note changes
    else ui::bluetoothRefresh(btWasScanning);
}

}  // namespace setup
}  // namespace app
