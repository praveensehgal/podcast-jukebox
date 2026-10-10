// Boot-time over-the-air update against a JSON manifest on a plain HTTPS host.
//
// Flow: GET manifest.json -> compare version -> GET firmware.bin streamed straight
// into the inactive OTA partition while hashing it -> verify sha256 -> set boot
// partition -> reboot. Any failure leaves the running image untouched.
//
// TLS trust comes from the ESP-IDF certificate bundle (Mozilla roots), so GitHub's
// certificate rotations do not brick the updater. No secrets live in this file.

#include "ota.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "http_get.h"
#include "version.h"

namespace ota {

static const char* kDefaultManifest =
    "https://github.com/praveensehgal/podcast-jukebox/releases/latest/download/manifest.json";
static const size_t kChunk = 4096;
static const uint32_t kMaxImage = 1900 * 1024;  // min_spiffs OTA slot size

// ---------- NVS-backed manifest URL ----------

String manifestUrl() {
    Preferences p;
    p.begin("ota", false);
    String url = p.getString("manifest", "");
    p.end();
    return url.length() ? url : String(kDefaultManifest);
}

void setManifestUrl(const String& url) {
    Preferences p;
    p.begin("ota", false);
    if (url.length()) p.putString("manifest", url); else p.remove("manifest");
    p.end();
}

// ---------- version compare ----------

static void splitVersion(const String& v, int out[3], bool& isDev) {
    String s = v;
    if (s.startsWith("v") || s.startsWith("V")) s = s.substring(1);
    isDev = s.indexOf('-') >= 0;
    int dash = s.indexOf('-');
    if (dash >= 0) s = s.substring(0, dash);
    out[0] = out[1] = out[2] = 0;
    int idx = 0, start = 0;
    while (idx < 3) {
        int dot = s.indexOf('.', start);
        String part = dot < 0 ? s.substring(start) : s.substring(start, dot);
        out[idx++] = part.toInt();
        if (dot < 0) break;
        start = dot + 1;
    }
}

int compareVersions(const String& a, const String& b) {
    int va[3], vb[3];
    bool devA, devB;
    splitVersion(a, va, devA);
    splitVersion(b, vb, devB);
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    if (devA != devB) return devA ? -1 : 1;  // 1.2.3-dev < 1.2.3
    return 0;
}

// ---------- manifest ----------

static bool fetchManifest(Manifest& m) {
    String body;
    if (!http_get::text(manifestUrl(), body, 4096)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        Serial.println("[ota] manifest is not valid JSON");
        return false;
    }
    m.version = doc["version"] | "";
    m.url = doc["url"] | "";
    m.sha256 = doc["sha256"] | "";
    m.size = doc["size"] | 0;
    m.sha256.toLowerCase();
    bool ok = m.version.length() && m.url.startsWith("https://") && m.sha256.length() == 64;
    if (!ok) Serial.println("[ota] manifest missing version/url/sha256");
    return ok;
}

// ---------- download + flash ----------

static String hexDigest(const unsigned char d[32]) {
    static const char* hex = "0123456789abcdef";
    String s;
    s.reserve(64);
    for (int i = 0; i < 32; i++) { s += hex[d[i] >> 4]; s += hex[d[i] & 0xF]; }
    return s;
}

static Result flashImage(const Manifest& m) {
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) { Serial.println("[ota] no OTA partition"); return Result::DownloadError; }
    if (m.size && m.size > kMaxImage) { Serial.println("[ota] image too large"); return Result::DownloadError; }

    esp_http_client_handle_t c = http_get::open(m.url, kChunk, 30000);
    if (!c) return Result::DownloadError;

    esp_ota_handle_t h = 0;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &h) != ESP_OK) {
        http_get::close(c);
        return Result::DownloadError;
    }
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    uint8_t* buf = (uint8_t*)malloc(kChunk);
    uint32_t total = 0;
    int n;
    bool ok = buf != nullptr;
    while (ok && (n = esp_http_client_read(c, (char*)buf, kChunk)) > 0) {
        if (esp_ota_write(h, buf, n) != ESP_OK) { ok = false; break; }
        mbedtls_sha256_update(&sha, buf, n);
        total += n;
        if (total > kMaxImage) { ok = false; break; }
        if ((total / kChunk) % 32 == 0) Serial.printf("[ota] %u KB\n", total / 1024);
    }
    free(buf);
    http_get::close(c);

    unsigned char digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    String got = hexDigest(digest);

    if (!ok || total == 0 || (m.size && total != m.size)) {
        esp_ota_abort(h);
        Serial.printf("[ota] download failed at %u bytes\n", total);
        return Result::DownloadError;
    }
    if (got != m.sha256) {
        esp_ota_abort(h);
        Serial.printf("[ota] sha256 mismatch\n  want %s\n  got  %s\n", m.sha256.c_str(), got.c_str());
        return Result::HashMismatch;
    }
    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        Serial.println("[ota] image rejected by esp_ota_end / set_boot_partition");
        return Result::DownloadError;
    }
    Serial.printf("[ota] flashed %u bytes to %s, rebooting into %s\n", total, part->label, m.version.c_str());
    return Result::Updated;
}

// ---------- entry point ----------

Result checkAndUpdate() {
    if (WiFi.status() != WL_CONNECTED) return Result::NoNetwork;
    Serial.printf("[ota] running %s, checking %s\n", fwVersion(), manifestUrl().c_str());

    Manifest m;
    if (!fetchManifest(m)) return Result::ManifestError;
    Serial.printf("[ota] latest %s (%u bytes)\n", m.version.c_str(), m.size);

    if (compareVersions(m.version, fwVersion()) <= 0) return Result::UpToDate;
    // A USB-flashed dev build must not replace itself with the last release, or every
    // bench session would end with the board silently running old firmware.
    if (strcmp(fwVersion(), "0.0.0-dev") == 0) {
        Serial.printf("[ota] dev build; %s is available but not installed\n", m.version.c_str());
        return Result::UpToDate;
    }

    Result r = flashImage(m);
    if (r == Result::Updated) {
        delay(500);
        ESP.restart();
    }
    return r;
}

const char* resultName(Result r) {
    switch (r) {
        case Result::UpToDate: return "up to date";
        case Result::Updated: return "updated";
        case Result::NoNetwork: return "no network";
        case Result::ManifestError: return "manifest error";
        case Result::DownloadError: return "download error";
        case Result::HashMismatch: return "hash mismatch";
    }
    return "?";
}

}  // namespace ota
