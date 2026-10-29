// Browse and playback: Channels, Episodes, Now Playing, and the three top buttons.
// The Settings family lives in app_setup.cpp.

#include "app.h"

#include <lvgl.h>

#include "app_internal.h"
#include "audio_out.h"
#include "resolver_client.h"
#include "settings.h"
#include "ui/ui.h"
#include "version.h"

namespace app {

Screen screen = Screen::Channels;
std::vector<channels::Channel>* chans = nullptr;

static std::vector<resolver::Episode> browsing;   // episodes of the channel being browsed
static int browsingChannel = -1;
static int channelsPageAtOpen = 0;

// The episode in the player is a copy, independent of whatever list is on screen (a
// position save must never index a list that has since been replaced). It outlives
// STOP so PLAY can pick it back up where he left off.
static resolver::Episode current;
static String currentChannel;     // by name: survives a channel-list refresh
static int playingChannel = -1;
static bool hasCurrent = false;   // something PLAY can resume
static bool active = false;       // audio loaded, playing or paused

static uint32_t lastSave = 0, lastTick = 0, sleepAt = 0;
static const int32_t kSkipS = 30;
static const int kVolStep = 2;
static const uint32_t kSaveEveryMs = 30000;
static const uint32_t kNearEndS = 45;   // an end this close to the length is the real end
static const int kMaxReopens = 3;
static int reopens = 0;

// Bench-only: NPR News Now episode of 2026-10-10 (dev builds, serial 't').
static const char* kBenchUrl =
    "https://prfx.byspotify.com/e/play.podtrac.com/npr-500005/npr.simplecastaudio.com/"
    "368f8510-314e-46f1-8d26-5b87ed6ab6eb/episodes/f2a7769f-e321-4bce-b9ee-2682911ae0e0/audio/128/default.mp3";

static bool validChannel(int i) { return chans && i >= 0 && i < (int)chans->size(); }
static bool playingChannelValid() {
    return validChannel(playingChannel) && (*chans)[playingChannel].name == currentChannel;
}
static uint32_t duration() { return current.durationS ? current.durationS : audio_out::durationSeconds(); }

// ---------- screens ----------

void showChannels() {
    screen = Screen::Channels;
    ui::channelsShow(*chans, channelsPageAtOpen, active && playingChannelValid() ? playingChannel : -1, active);
}

static void showEpisodes() {
    if (!validChannel(browsingChannel)) { showChannels(); return; }
    screen = Screen::Episodes;
    int at = 0;
    bool here = active && browsingChannel == playingChannel;
    if (here)
        for (size_t i = 0; i < browsing.size(); i++) if (browsing[i].id == current.id) at = (int)i;
    ui::episodesShow((*chans)[browsingChannel].name.c_str(), browsing, at / 3, here ? current.id : String(), active);
}

static void openChannel(int idx) {
    if (!validChannel(idx)) return;
    if (screen == Screen::Channels) channelsPageAtOpen = ui::channelsPage();
    browsingChannel = idx;
    const channels::Channel& c = (*chans)[idx];
    ui::noticeShow(c.name.c_str(), "Fetching episodes...");
    if (!resolver::episodes(c.feed, browsing)) {
        // An unreachable server is not an empty channel; say which it is, then go back.
        browsing.clear();
        ui::noticeShow("Can't reach the podcast server.", "Tap the channel to try again.");
        delay(2500);
        showChannels();
        return;
    }
    showEpisodes();
}

static void refreshPlaying() {
    ui::playingUpdate(audio_out::positionSeconds(), duration(), audio_out::isPaused(), settings::volume());
}

static void showPlaying() {
    char initials[4];
    uint32_t color;
    ui::channelArt(currentChannel.c_str(), initials, color);
    screen = Screen::Playing;
    ui::playingShow(currentChannel.c_str(), initials, color, current);
    refreshPlaying();
}

// ---------- playback ----------

// Saving is two steps so STOP can close the stream in between: with a TLS stream open
// the board has roughly 6-14 KB of usable heap, and the POST to the resolver failed
// there (measured 2026-10-10). The local copy alone is enough for PLAY to resume.
static void notePosition() {
    if (!active) return;
    current.posS = audio_out::positionSeconds();
    for (auto& e : browsing) if (e.id == current.id) e.posS = current.posS;
}

static void pushPosition() {
    if (current.id.length()) resolver::savePosition(current.id, current.posS, duration());
    lastSave = millis();
}

static void savePosition() {
    if (!active) return;
    notePosition();
    pushPosition();
}

static bool startAudio(uint32_t from) {
    String url = current.id.length() ? resolver::streamUrl(current) : current.url;
    return audio_out::play(url, from, current.sizeBytes, current.durationS);
}

static void beginPlayback(const resolver::Episode& e, const String& channelName, int channelIdx) {
    resolver::Episode next = e;   // `e` may be `current` or a row stopPlayback() updates
    stopPlayback();
    current = next;
    currentChannel = channelName;
    playingChannel = channelIdx;
    hasCurrent = true;
    ui::noticeShow(current.title.c_str(), "Starting...");
    uint32_t from = (current.posS && !current.played) ? current.posS : 0;
    current.played = false;   // a finished episode played again is in progress again
    active = startAudio(from);
    if (!active) {
        ui::noticeShow("Could not play this episode.", "Try again in a minute.");
        delay(2500);
        screen == Screen::Episodes ? showEpisodes() : showChannels();
        return;
    }
    lastSave = millis();
    reopens = 0;
    uint16_t m = settings::sleepMinutes();
    sleepAt = m ? millis() + m * 60000UL : 0;
    showPlaying();
}

void stopPlayback() {
    bool was = active;
    notePosition();
    audio_out::stop();   // give the stream's memory back before talking to the resolver
    active = false;
    sleepAt = 0;
    if (was) pushPosition();
}

static void stopAll() {
    stopPlayback();
    setup::cancel();
    showChannels();
}

static void togglePause() {
    if (!active) return;
    audio_out::pause(!audio_out::isPaused());
    if (audio_out::isPaused()) savePosition();
    if (screen == Screen::Playing) refreshPlaying();
}

static void playButton() {
    if (active) {
        if (audio_out::isPaused()) togglePause();
        else if (screen != Screen::Playing) showPlaying();
        return;
    }
    if (hasCurrent) beginPlayback(current, String(currentChannel), playingChannel);
}

// Playback gave up. Never leave a screen with nothing to tap: say so, then go back to
// the list, where tapping the episode (or PLAY) resumes from the same second.
static void lostStream() {
    for (auto& e : browsing) if (e.id == current.id) e.posS = current.posS;
    audio_out::stop();
    active = false;   // hasCurrent stays
    sleepAt = 0;
    ui::noticeShow("Lost the stream.", "Tap the episode to try again.");
    delay(2500);
    (browsingChannel == playingChannel && !browsing.empty()) ? showEpisodes() : showChannels();
}

static void skip(int32_t delta) {
    if (!active || !current.sizeBytes || !current.durationS) return;   // no byte map, no seek
    int32_t t = max<int32_t>(0, (int32_t)audio_out::positionSeconds() + delta);
    if (!audio_out::seekTo(t) && !startAudio(t)) {   // second try from scratch
        Serial.println("[app] skip failed");
        lostStream();
        return;
    }
    current.posS = t;
    if (screen == Screen::Playing) refreshPlaying();
}

static void volume(int dir) {
    int v = constrain((int)settings::volume() + dir * kVolStep, 0, 16);
    settings::setVolume(v);
    audio_out::setVolume(v);
    if (screen == Screen::Playing) refreshPlaying();
}

static void finishedEpisode() {
    uint32_t dur = duration() ? duration() : audio_out::positionSeconds();
    if (current.id.length()) resolver::savePosition(current.id, dur, dur);
    for (auto& e : browsing) if (e.id == current.id) { e.posS = dur; e.played = true; }
    audio_out::stop();
    active = false;
    hasCurrent = false;
    sleepAt = 0;
    if (screen == Screen::Playing) browsingChannel == playingChannel ? showEpisodes() : showChannels();
}

// The stream stopped. Near the end it is the end; anywhere else the connection dropped
// (a long pause does it), so pick up from the same place a few times before giving up.
static void streamEnded() {
    uint32_t pos = audio_out::positionSeconds(), dur = duration();
    if (!dur || pos + kNearEndS >= dur) { finishedEpisode(); return; }
    current.posS = pos;
    if (reopens++ < kMaxReopens && startAudio(pos)) {
        Serial.printf("[app] stream dropped at %u s, reopened\n", (unsigned)pos);
        return;
    }
    lostStream();
}

static void benchPlay() {
    if (strcmp(fwVersion(), "0.0.0-dev") != 0) return;
    resolver::Episode e;
    e.title = "NPR News Now, 10 Oct (bench)";
    e.date = "OCT 10";
    e.durationS = 280;
    e.sizeBytes = 5266737;
    e.url = kBenchUrl;
    beginPlayback(e, "NPR News Now", -1);
}

// ---------- events ----------

static void back() {
    if (screen == Screen::Episodes) { showChannels(); return; }
    if (screen != Screen::Playing) { setup::back(); return; }
    if (!playingChannelValid()) showChannels();
    else if (browsingChannel == playingChannel) showEpisodes();
    else openChannel(playingChannel);
}

static void pick(int i) {
    if (screen == Screen::Channels) { openChannel(i); return; }
    if (screen != Screen::Episodes) { setup::pick(i); return; }
    if (i < 0 || i >= (int)browsing.size()) return;
    if (active && browsingChannel == playingChannel && browsing[i].id == current.id) { showPlaying(); return; }
    beginPlayback(browsing[i], (*chans)[browsingChannel].name, browsingChannel);
}

void begin(std::vector<channels::Channel>* list, const String& hotspotKey) {
    chans = list;
    setup::begin(hotspotKey);
    audio_out::setVolume(settings::volume());
    showChannels();
}

void handle(const input::Event& e) {
    using input::Kind;
    switch (e.kind) {
        case Kind::Play: playButton(); break;
        case Kind::Pause: togglePause(); break;
        case Kind::Stop: stopAll(); break;
        case Kind::StopHold: setup::cancel(); setup::openSettings(0); break;
        case Kind::Gear: setup::openSettings(0); break;
        case Kind::NowPlaying: if (active) showPlaying(); break;
        case Kind::VolDown: volume(-1); break;
        case Kind::VolUp: volume(+1); break;
        case Kind::SkipBack: skip(-kSkipS); break;
        case Kind::SkipFwd: skip(kSkipS); break;
        case Kind::Back: back(); break;
        case Kind::Pick: pick(e.value); break;
        case Kind::Join: setup::join(); break;
        case Kind::BenchPlay: benchPlay(); break;
        default: break;
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
    setup::loop();
    if (millis() - lastTick < 1000) return;
    lastTick = millis();
    memReport();
    if (!active) return;
    if (screen == Screen::Playing) refreshPlaying();
    if (!audio_out::isPaused() && millis() - lastSave > kSaveEveryMs) { savePosition(); reopens = 0; }
    if (sleepAt && millis() > sleepAt) { Serial.println("[app] sleep timer"); stopAll(); return; }
    if (!audio_out::isPlaying() && !audio_out::isPaused()) streamEnded();
}

}  // namespace app
