#pragma once
#include <Arduino.h>
#include <vector>

namespace channels {

struct Channel {
    uint8_t position;  // 1..20, screen order
    String name;       // as shown on the channel list
    String feed;       // RSS URL
};

static const size_t kMaxChannels = 20;

// Where channels.md is fetched from. Default: this repo's rolling "channels" release.
// Overridable from NVS (namespace "ota", key "channels").
String sourceUrl();

// Parse the markdown table in `md`. Tolerates the heading, prose and the header row;
// only rows that look like `| n | name | https://... |` are kept. Returns rows in
// position order, at most kMaxChannels.
std::vector<Channel> parse(const String& md);

// Fetch channels.md over HTTPS, parse, and on success cache the raw file in flash.
// On any failure, fall back to the cached copy. Returns the list (possibly empty).
std::vector<Channel> load(bool& fromCache);

}  // namespace channels
