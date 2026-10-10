#include "app.h"

#include <lvgl.h>

#include "audio_out.h"
#include "resolver_client.h"
#include "ui/ui.h"
#include "version.h"

namespace app {

enum class Screen { Channels, Episodes, Playing };

static std::vector<channels::Channel>* chans = nullptr;
static std::vector<resolver::Episode> episodes;
static Screen screen = Screen::Channels;
static int channelIdx = 0;        // channel whose episodes are loaded
static int playingEp = -1;        // index into `episodes`, -1 = nothing
static int playingChannel = -1;   // channel of the playing episode
static uint32_t lastSave = 0;
static uint32_t lastTick = 0;

// Bench-only: NPR News Now episode of 2026-10-10 (dev builds, serial 't').
static const char* kBenchUrl =
    "https://prfx.byspotify.com/e/play.podtrac.com/npr-500005/npr.simplecastaudio.com/"
    "368f8510-314e-46f1-8d26-5b87ed6ab6eb/episodes/f2a7769f-e321-4bce-b9ee-2682911ae0e0/audio/128/default.mp3";

static void showChannels() {
    screen = Screen::Channels;
    ui::channelsShow(*chans, channelIdx);
}

static void openChannel(int idx) {
    channelIdx = idx;
    const channels::Channel& c = (*chans)[idx];
    ui::noticeShow(c.name.c_str(), "Fetching episodes...");
    if (!resolver::episodes(c.feed, episodes)) episodes.clear();
    screen = Screen::Episodes;
    int sel = (playingChannel == idx && playingEp >= 0) ? playingEp : 0;
    ui::episodesShow(c.name.c_str(), episodes, sel);
}

static void savePosition() {
    if (playingEp < 0 || playingEp >= (int)episodes.size()) return;
    resolver::Episode& e = episodes[playingEp];
    e.posS = audio_out::positionSeconds();
    resolver::savePosition(e.id, e.posS, e.durationS ? e.durationS : audio_out::durationSeconds());
    lastSave = millis();
}

static void showPlaying() {
    const channels::Channel& c = (*chans)[playingChannel];
    const resolver::Episode& e = episodes[playingEp];
    char initials[4];
    uint32_t color;
    ui::channelArt(c.name.c_str(), initials, color);
    screen = Screen::Playing;
    ui::playingShow(c.name.c_str(), initials, color, e.title.c_str());
    ui::playingUpdate(audio_out::positionSeconds(), audio_out::durationSeconds(), audio_out::isPaused());
}

static void playEpisode(int idx) {
    if (idx < 0 || idx >= (int)episodes.size()) return;
    if (playingEp >= 0 && audio_out::isPlaying()) savePosition();
    resolver::Episode& e = episodes[idx];
    ui::noticeShow(e.title.c_str(), "Starting...");
    uint32_t resume = (e.posS && !e.played) ? e.posS : 0;
    String url = resolver::freshUrl(e);
    if (!audio_out::play(url, resume, e.sizeBytes, e.durationS)) {
        ui::noticeShow("Could not play this episode.", "Press STOP to go back.");
        return;
    }
    playingEp = idx;
    playingChannel = channelIdx;
    lastSave = millis();
    showPlaying();
}

static void stopAll() {
    if (playingEp >= 0) savePosition();
    audio_out::stop();
    playingEp = -1;
    playingChannel = -1;
    showChannels();
}

void begin(std::vector<channels::Channel>* list) {
    chans = list;
    showChannels();
}

void handle(const input::Event& e) {
    using input::Kind;
    if (e.kind == Kind::Volume) { ui::volumeFlash(e.value); audio_out::setVolume(e.value); return; }
    if (e.kind == Kind::Stop) { stopAll(); return; }
    if (e.kind == Kind::Pause) {
        if (playingEp >= 0) { audio_out::pause(!audio_out::isPaused()); if (audio_out::isPaused()) savePosition(); }
        return;
    }
    if (e.kind == Kind::BenchPlay) {
        if (strcmp(fwVersion(), "0.0.0-dev") == 0) audio_out::play(kBenchUrl, 0, 5266737, 280);
        return;
    }
    switch (screen) {
        case Screen::Channels:
            if (ui::channelsHandle(e)) return;
            if (e.kind == Kind::Ok && !chans->empty()) openChannel(ui::channelsSelected());
            break;
        case Screen::Episodes:
            if (ui::episodesHandle(e)) return;
            if (e.kind == Kind::Ok && !episodes.empty()) playEpisode(ui::episodesSelected());
            break;
        case Screen::Playing:
            if (e.kind == Kind::Scroll && playingEp >= 0) { /* 30 s skip: later, needs seek-by-time */ }
            if (e.kind == Kind::Ok) { screen = Screen::Episodes; ui::episodesShow((*chans)[playingChannel].name.c_str(), episodes, playingEp); }
            break;
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
    if (millis() - lastTick < 1000) return;
    lastTick = millis();
    memReport();
    if (playingEp < 0) return;
    if (screen == Screen::Playing)
        ui::playingUpdate(audio_out::positionSeconds(), audio_out::durationSeconds(), audio_out::isPaused());
    if (!audio_out::isPaused() && millis() - lastSave > 30000) savePosition();
    if (!audio_out::isPlaying() && !audio_out::isPaused()) {
        // Episode ended on its own: mark it played and return to the list.
        resolver::Episode& e = episodes[playingEp];
        resolver::savePosition(e.id, e.durationS ? e.durationS : audio_out::positionSeconds(), e.durationS);
        e.posS = e.durationS;
        e.played = true;
        playingEp = -1;
        screen = Screen::Episodes;
        ui::episodesShow((*chans)[channelIdx].name.c_str(), episodes, 0);
    }
}

}  // namespace app
