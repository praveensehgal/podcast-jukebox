#include "app.h"

#include <WiFi.h>
#include <lvgl.h>

#include "audio_out.h"
#include "bt_emitter.h"
#include "resolver_client.h"
#include "settings.h"
#include "ui/ui.h"
#include "version.h"

namespace app {

enum class Screen { Channels, Episodes, Playing, Settings, Wifi, Password, Bluetooth };

static std::vector<channels::Channel>* chans = nullptr;
static std::vector<resolver::Episode> browsing;   // list of the channel being browsed
static int browsingChannel = -1;
static Screen screen = Screen::Channels;

// What is playing is a copy, independent of whatever list is on screen (a position
// save must never index a list that has since been replaced).
static resolver::Episode nowPlaying;
static int playingChannel = -1;
static bool hasPlaying = false;

static uint32_t lastSave = 0;
static uint32_t lastTick = 0;
static uint32_t sleepAt = 0;
static String pendingSsid;
static const char* setupSsid = "Jukebox-Setup";
static String setupKey;

// Bench-only: NPR News Now episode of 2026-10-10 (dev builds, serial 't').
static const char* kBenchUrl =
    "https://prfx.byspotify.com/e/play.podtrac.com/npr-500005/npr.simplecastaudio.com/"
    "368f8510-314e-46f1-8d26-5b87ed6ab6eb/episodes/f2a7769f-e321-4bce-b9ee-2682911ae0e0/audio/128/default.mp3";

// ---------- navigation ----------

static void showChannels() {
    screen = Screen::Channels;
    ui::channelsShow(*chans, browsingChannel < 0 ? 0 : browsingChannel);
}

static void showEpisodes(int sel) {
    screen = Screen::Episodes;
    ui::episodesShow((*chans)[browsingChannel].name.c_str(), browsing, sel);
}

static int playingIndexInBrowsing() {
    if (!hasPlaying || browsingChannel != playingChannel) return 0;
    for (size_t i = 0; i < browsing.size(); i++) if (browsing[i].id == nowPlaying.id) return (int)i;
    return 0;
}

static void openChannel(int idx) {
    browsingChannel = idx;
    const channels::Channel& c = (*chans)[idx];
    ui::noticeShow(c.name.c_str(), "Fetching episodes...");
    if (!resolver::episodes(c.feed, browsing)) browsing.clear();
    showEpisodes(playingIndexInBrowsing());
}

static void showPlaying() {
    const channels::Channel& c = (*chans)[playingChannel];
    char initials[4];
    uint32_t color;
    ui::channelArt(c.name.c_str(), initials, color);
    screen = Screen::Playing;
    ui::playingShow(c.name.c_str(), initials, color, nowPlaying.title.c_str());
    ui::playingUpdate(audio_out::positionSeconds(), audio_out::durationSeconds(), audio_out::isPaused());
}

// ---------- playback ----------

static void savePosition() {
    if (!hasPlaying) return;
    nowPlaying.posS = audio_out::positionSeconds();
    uint32_t dur = nowPlaying.durationS ? nowPlaying.durationS : audio_out::durationSeconds();
    resolver::savePosition(nowPlaying.id, nowPlaying.posS, dur);
    for (auto& e : browsing) if (e.id == nowPlaying.id) e.posS = nowPlaying.posS;
    lastSave = millis();
}

static void playEpisode(int idx) {
    if (idx < 0 || idx >= (int)browsing.size()) return;
    if (hasPlaying) savePosition();
    audio_out::stop();
    resolver::Episode e = browsing[idx];
    ui::noticeShow(e.title.c_str(), "Starting...");
    uint32_t resume = (e.posS && !e.played) ? e.posS : 0;
    String url = resolver::freshUrl(e);
    if (!audio_out::play(url, resume, e.sizeBytes, e.durationS)) {
        hasPlaying = false;
        ui::noticeShow("Could not play this episode.", "Press STOP to go back.");
        return;
    }
    nowPlaying = e;
    playingChannel = browsingChannel;
    hasPlaying = true;
    lastSave = millis();
    uint16_t sleepMin = settings::sleepMinutes();
    sleepAt = sleepMin ? millis() + sleepMin * 60000UL : 0;
    showPlaying();
}

static void stopAll() {
    if (hasPlaying) savePosition();
    audio_out::stop();
    hasPlaying = false;
    playingChannel = -1;
    sleepAt = 0;
    showChannels();
}

static void finishedEpisode() {
    uint32_t dur = nowPlaying.durationS ? nowPlaying.durationS : audio_out::positionSeconds();
    resolver::savePosition(nowPlaying.id, dur, nowPlaying.durationS);
    for (auto& e : browsing) if (e.id == nowPlaying.id) { e.posS = dur; e.played = true; }
    hasPlaying = false;
    if (screen == Screen::Playing) {
        if (browsingChannel == playingChannel) showEpisodes(0); else showChannels();
    }
    playingChannel = -1;
}

// ---------- settings family ----------

static void openSettings() {
    screen = Screen::Settings;
    ui::settingsShow(0);
}

static void openWifi() {
    if (hasPlaying) { savePosition(); audio_out::stop(); hasPlaying = false; }
    ui::noticeShow("Looking for networks...", nullptr);
    ui::wifiScan();
    screen = Screen::Wifi;
    ui::wifiShow(setupSsid, setupKey.c_str());
}

static void joinNetwork(const String& ssid, const String& pass) {
    ui::noticeShow(ssid.c_str(), "Connecting...");
    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), pass.c_str());  // credentials persist in NVS by default
    uint32_t until = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < until) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[net] joined %s  ip %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
        ui::noticeShow("Connected", ssid.c_str());
    } else {
        ui::noticeShow("Could not connect.", "Check the password and try again.");
    }
    delay(1500);
    openSettings();
}

static void settingsAction(int row) {
    switch (row) {
        case 0: openWifi(); break;
        case 1: screen = Screen::Bluetooth; ui::bluetoothShow(); break;
        case 2:
            settings::setOutput(settings::output() == settings::Output::Speaker ? settings::Output::Bluetooth
                                                                                 : settings::Output::Speaker);
            ui::settingsRefresh();
            break;
        case 3: {
            uint16_t m = settings::sleepMinutes();
            settings::setSleepMinutes(m == 0 ? 30 : (m == 30 ? 60 : (m == 60 ? 90 : 0)));
            ui::settingsRefresh();
            break;
        }
        case 4: {
            bool cached = false;
            *chans = channels::load(cached);
            ui::noticeShow("Channel list refreshed", cached ? "(saved copy, no network)" : nullptr);
            delay(1200);
            ui::settingsShow(4);
            break;
        }
        case 5: {
            String l2 = String("Device ") + resolver::deviceId();
            ui::noticeShow((String("Podcast Jukebox v") + fwVersion()).c_str(), l2.c_str());
            delay(2500);
            ui::settingsShow(5);
            break;
        }
        default: showChannels(); break;
    }
}

static void bluetoothActionDo() {
    int a = ui::bluetoothAction();
    if (a >= 0) {
        bt::connect(bt::devices()[a]);
        settings::setOutput(settings::Output::Bluetooth);
    } else if (a == -1) {
        settings::setOutput(settings::output() == settings::Output::Speaker ? settings::Output::Bluetooth
                                                                             : settings::Output::Speaker);
    } else if (a == -2) {
        bt::disconnect();
        if (!bt::devices().empty()) bt::forget(bt::devices()[0]);
        settings::setOutput(settings::Output::Speaker);
    } else if (a == -3) {
        openSettings();
        return;
    }
    ui::bluetoothRefresh();
}

// ---------- public ----------

void begin(std::vector<channels::Channel>* list, const String& hotspotKey) {
    chans = list;
    setupKey = hotspotKey;
    audio_out::setVolume(settings::volume());
    showChannels();
}

static void handleGlobal(const input::Event& e, bool& consumed) {
    using input::Kind;
    consumed = true;
    if (e.kind == Kind::Volume) { ui::volumeFlash(e.value); audio_out::setVolume(e.value); settings::setVolume(e.value); return; }
    if (e.kind == Kind::StopHold) { if (screen != Screen::Password) openSettings(); return; }
    if (e.kind == Kind::BenchPlay) {
        if (strcmp(fwVersion(), "0.0.0-dev") == 0) audio_out::play(kBenchUrl, 0, 5266737, 280);
        return;
    }
    consumed = false;
}

void handle(const input::Event& e) {
    using input::Kind;
    bool consumed;
    handleGlobal(e, consumed);
    if (consumed) return;

    switch (screen) {
        case Screen::Channels:
            if (e.kind == Kind::Stop) { stopAll(); return; }
            if (e.kind == Kind::Pause) { if (hasPlaying) { audio_out::pause(!audio_out::isPaused()); if (audio_out::isPaused()) savePosition(); } return; }
            if (ui::channelsHandle(e)) return;
            if (e.kind == Kind::Ok && !chans->empty()) {
                int sel = ui::channelsSelected();
                if (sel >= (int)chans->size()) openSettings(); else openChannel(sel);
            }
            return;
        case Screen::Episodes:
            if (e.kind == Kind::Stop) { stopAll(); return; }
            if (e.kind == Kind::Pause) { if (hasPlaying) { audio_out::pause(!audio_out::isPaused()); if (audio_out::isPaused()) savePosition(); } return; }
            if (ui::episodesHandle(e)) return;
            if (e.kind == Kind::Ok && !browsing.empty()) playEpisode(ui::episodesSelected());
            return;
        case Screen::Playing:
            if (e.kind == Kind::Stop) { stopAll(); return; }
            if (e.kind == Kind::Pause) { audio_out::pause(!audio_out::isPaused()); if (audio_out::isPaused()) savePosition(); return; }
            if (e.kind == Kind::Ok) { browsingChannel = playingChannel; showEpisodes(playingIndexInBrowsing()); }
            return;
        case Screen::Settings:
            if (e.kind == Kind::Stop) { showChannels(); return; }
            if (ui::settingsHandle(e)) return;
            if (e.kind == Kind::Ok) settingsAction(ui::settingsSelected());
            return;
        case Screen::Wifi:
            if (e.kind == Kind::Stop) { openSettings(); return; }
            if (e.kind == Kind::Pause) { openWifi(); return; }  // rescan
            if (ui::wifiHandle(e)) return;
            if (e.kind == Kind::Ok) {
                pendingSsid = ui::wifiSelected();
                if (!pendingSsid.length()) return;  // "Type a network name": later
                screen = Screen::Password;
                ui::passwordShow(pendingSsid.c_str());
            }
            return;
        case Screen::Password: {
            if (e.kind == Kind::Stop) { openWifi(); return; }
            int r = ui::passwordHandle(e);
            if (r == 1) joinNetwork(pendingSsid, ui::passwordText());
            return;
        }
        case Screen::Bluetooth:
            if (e.kind == Kind::Stop) { openSettings(); return; }
            if (e.kind == Kind::Pause) { bt::startScan(); ui::bluetoothRefresh(); return; }
            if (ui::bluetoothHandle(e)) return;
            if (e.kind == Kind::Ok) bluetoothActionDo();
            return;
    }
}

static void memReport() {
    static uint32_t last = 0;
    if (millis() - last < 10000) return;
    last = millis();
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    Serial.printf("[mem] heap %u  largest %u  lvgl %u%% used (%u frag)\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(), (unsigned)m.used_pct, (unsigned)m.frag_pct);
}

void loop() {
    ui::passwordTick();
    bt::loop();
    if (screen == Screen::Bluetooth) { static uint32_t t = 0; if (millis() - t > 2000) { t = millis(); ui::bluetoothRefresh(); } }
    if (millis() - lastTick < 1000) return;
    lastTick = millis();
    memReport();
    if (!hasPlaying) return;
    if (screen == Screen::Playing)
        ui::playingUpdate(audio_out::positionSeconds(), audio_out::durationSeconds(), audio_out::isPaused());
    if (!audio_out::isPaused() && millis() - lastSave > 30000) savePosition();
    if (sleepAt && millis() > sleepAt) { Serial.println("[app] sleep timer"); stopAll(); return; }
    if (!audio_out::isPlaying() && !audio_out::isPaused()) finishedEpisode();
}

}  // namespace app
