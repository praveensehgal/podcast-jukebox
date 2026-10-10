// Channel list: a markdown table in the git repo, fetched at boot, cached in LittleFS.

#include "channels.h"

#include <algorithm>

#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include "version.h"

namespace channels {

static const char* kDefaultSource =
    "https://github.com/praveensehgal/podcast-jukebox/releases/download/channels/channels.md";
static const char* kCachePath = "/channels.md";
static const size_t kMaxBytes = 16 * 1024;

String sourceUrl() {
    Preferences p;
    p.begin("ota", true);
    String url = p.getString("channels", "");
    p.end();
    return url.length() ? url : String(kDefaultSource);
}

// ---------- parsing ----------

static String cell(const String& row, int index) {
    // row is "| a | b | c |"; return trimmed cell `index` (0-based) or "".
    int start = 0, seen = -1;
    while (true) {
        int bar = row.indexOf('|', start);
        if (bar < 0) return "";
        if (seen == index) return row.substring(start, bar);
        seen++;
        start = bar + 1;
        if (seen == index) {
            int next = row.indexOf('|', start);
            if (next < 0) return "";
            String s = row.substring(start, next);
            s.trim();
            return s;
        }
    }
}

static bool parseRow(const String& line, Channel& out) {
    String l = line;
    l.trim();
    if (!l.startsWith("|")) return false;
    String pos = cell(l, 0), name = cell(l, 1), feed = cell(l, 2);
    if (!pos.length() || pos.startsWith("-") || pos.equalsIgnoreCase("#")) return false;
    int n = pos.toInt();
    if (n < 1 || n > (int)kMaxChannels) return false;
    if (!feed.startsWith("https://") || !name.length()) return false;
    out.position = (uint8_t)n;
    out.name = name;
    out.feed = feed;
    return true;
}

std::vector<Channel> parse(const String& md) {
    std::vector<Channel> list;
    int start = 0;
    while (start < (int)md.length() && list.size() < kMaxChannels) {
        int nl = md.indexOf('\n', start);
        String line = nl < 0 ? md.substring(start) : md.substring(start, nl);
        Channel c;
        if (parseRow(line, c)) list.push_back(c);
        if (nl < 0) break;
        start = nl + 1;
    }
    std::sort(list.begin(), list.end(),
              [](const Channel& a, const Channel& b) { return a.position < b.position; });
    return list;
}

// ---------- fetch + cache ----------

static bool fetch(String& body) {
    if (WiFi.status() != WL_CONNECTED) return false;
    esp_http_client_config_t cfg = {};
    String url = sourceUrl();
    cfg.url = url.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.max_redirection_count = 5;
    cfg.user_agent = FW_NAME "/0";
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    bool ok = false;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status == 200) {
            char buf[512];
            int n;
            while ((n = esp_http_client_read(c, buf, sizeof(buf))) > 0 && body.length() < kMaxBytes)
                body.concat(buf, n);
            ok = body.length() > 0;
        } else {
            Serial.printf("[channels] GET -> HTTP %d\n", status);
        }
        esp_http_client_close(c);
    }
    esp_http_client_cleanup(c);
    return ok;
}

static String readCache() {
    if (!LittleFS.exists(kCachePath)) return "";
    File f = LittleFS.open(kCachePath, "r");
    String s = f.readString();
    f.close();
    return s;
}

static void writeCache(const String& md) {
    File f = LittleFS.open(kCachePath, "w");
    if (!f) return;
    f.print(md);
    f.close();
}

std::vector<Channel> load(bool& fromCache) {
    static bool fsReady = LittleFS.begin(true);
    (void)fsReady;
    String md;
    fromCache = false;
    if (fetch(md)) {
        std::vector<Channel> list = parse(md);
        if (!list.empty()) {
            writeCache(md);
            return list;
        }
        Serial.println("[channels] fetched file had no valid rows; using cache");
    }
    fromCache = true;
    return parse(readCache());
}

}  // namespace channels
