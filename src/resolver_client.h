#pragma once
#include <Arduino.h>

#include <vector>

// Client for the resolver service (resolver/ in this repo). The ESP32 never parses
// RSS; it asks the resolver for compact episode lists and keeps listening positions
// there, keyed by this device's MAC.
namespace resolver {

struct Episode {
    String id;        // resolver's stable 16-char id
    String title;     // already stripped and capped by the resolver
    String date;      // "OCT 9" style, derived from published
    uint32_t durationS = 0;
    uint32_t sizeBytes = 0;
    String url;       // direct audio URL (may be re-resolved just before play)
    uint32_t posS = 0;        // listening position from /positions, 0 = unplayed
    bool played = false;      // finished (pos within 60 s of the end)
};

// Base URL, NVS namespace "ota" key "resolver"; default http://jukebox.sehgalp.com (EC2).
// The access token is NVS "ota" key "token", set over serial with `#token <value>`; it
// is never compiled in (the repo is public, decision 003). begin() applies both to
// http_get, and the setters re-apply them. An empty value clears the NVS key.
void begin();
String baseUrl();
void setBaseUrl(const String& url);
String token();
void setToken(const String& token);

// Newest-first episodes for a feed, merged with this device's positions.
bool episodes(const String& feed, std::vector<Episode>& out, int limit = 12);

// Positions, sent on pause/stop and every 30 s while playing.
bool savePosition(const String& episodeId, uint32_t posS, uint32_t durationS);

// Where to play an episode from: the resolver relays the audio over plain HTTP
// (decision 005). The device cannot afford a TLS session next to the MP3 decoder.
String streamUrl(const Episode& e);

String deviceId();

}  // namespace resolver
