#include "resolver_client.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_http_client.h>

#include <map>

#include "http_get.h"

namespace resolver {

// EC2 behind nginx. Plain HTTP on purpose: no TLS session next to the decoder (decision 005).
static const char* kDefaultBase = "http://jukebox.sehgalp.com";

static String readPref(const char* key) {
    Preferences p;
    p.begin("ota", false);   // read-only fails (and logs) while the namespace does not exist yet
    String v = p.getString(key, "");
    p.end();
    return v;
}

static void writePref(const char* key, const String& value) {
    Preferences p;
    p.begin("ota", false);
    if (value.length()) p.putString(key, value); else p.remove(key);
    p.end();
}

String baseUrl() {
    String url = readPref("resolver");
    return url.length() ? url : String(kDefaultBase);
}

String token() { return readPref("token"); }

void begin() { http_get::setAuth(baseUrl(), token()); }

void setBaseUrl(const String& url) {
    writePref("resolver", url);
    begin();
}

void setToken(const String& t) {
    writePref("token", t);
    begin();
}

String deviceId() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    return mac;
}

static String urlEncode(const String& s) {
    String out;
    out.reserve(s.length() * 3);
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); out += b; }
    }
    return out;
}

static const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static String shortDate(const char* iso) {
    // "2026-10-09T14:00:00Z" -> "OCT 9"
    if (!iso || strlen(iso) < 10) return "";
    int m = atoi(iso + 5), d = atoi(iso + 8);
    if (m < 1 || m > 12) return "";
    return String(kMonths[m - 1]) + " " + String(d);
}

static bool getJson(const String& url, JsonDocument& doc, const JsonDocument* filter) {
    String body;
    if (!http_get::text(url, body, 24 * 1024)) return false;
    DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                      : deserializeJson(doc, body);
    if (err) { Serial.printf("[resolver] bad JSON from %s: %s\n", url.c_str(), err.c_str()); return false; }
    if (!doc["ok"].as<bool>()) { Serial.printf("[resolver] error: %s\n", doc["error"] | "?"); return false; }
    return true;
}

static void loadPositions(std::map<String, std::pair<uint32_t, uint32_t>>& pos) {
    JsonDocument doc;
    if (!getJson(baseUrl() + "/positions?device=" + deviceId(), doc, nullptr)) return;
    for (JsonObject p : doc["data"].as<JsonArray>()) {
        pos[String(p["episode"] | "")] = {p["pos_s"] | 0U, p["duration_s"] | 0U};
    }
}

bool episodes(const String& feed, std::vector<Episode>& out, int limit) {
    out.clear();
    JsonDocument filter;
    JsonObject item = filter["data"].add<JsonObject>();
    item["id"] = true; item["title"] = true; item["published"] = true;
    item["duration_s"] = true; item["url"] = true; item["size"] = true;
    filter["ok"] = true; filter["error"] = true;

    JsonDocument doc;
    String url = baseUrl() + "/episodes?feed=" + urlEncode(feed) + "&limit=" + String(limit);
    if (!getJson(url, doc, &filter)) return false;

    std::map<String, std::pair<uint32_t, uint32_t>> pos;
    loadPositions(pos);

    for (JsonObject e : doc["data"].as<JsonArray>()) {
        Episode ep;
        ep.id = e["id"] | "";
        ep.title = e["title"] | "";
        ep.date = shortDate(e["published"] | "");
        ep.durationS = e["duration_s"] | 0U;
        ep.sizeBytes = e["size"] | 0U;
        ep.url = e["url"] | "";
        auto it = pos.find(ep.id);
        if (it != pos.end()) {
            ep.posS = it->second.first;
            uint32_t dur = ep.durationS ? ep.durationS : it->second.second;
            ep.played = dur && ep.posS + 60 >= dur;
        }
        out.push_back(ep);
    }
    Serial.printf("[resolver] %u episodes for %s\n", (unsigned)out.size(), feed.c_str());
    return true;
}

bool savePosition(const String& episodeId, uint32_t posS, uint32_t durationS) {
    String url = baseUrl() + "/position";
    JsonDocument doc;
    doc["device"] = deviceId();
    doc["episode"] = episodeId;
    doc["pos_s"] = posS;
    doc["duration_s"] = durationS;
    String body;
    serializeJson(doc, body);

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 8000;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    http_get::addAuth(c, url);
    esp_http_client_set_post_field(c, body.c_str(), body.length());
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    bool ok = err == ESP_OK && status == 200;
    if (!ok) Serial.printf("[resolver] position save failed (%d)\n", status);
    return ok;
}

String streamUrl(const Episode& e) {
    return baseUrl() + "/stream?url=" + urlEncode(e.url);
}

}  // namespace resolver
