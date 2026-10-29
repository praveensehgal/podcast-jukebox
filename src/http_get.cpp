#include "http_get.h"

#include <algorithm>

#include <esp_crt_bundle.h>

#include "version.h"

namespace http_get {

static const int kMaxRedirects = 5;
static const char* kAuthHeader = "X-Jukebox-Token";
static String authPrefix;
static String authToken;

void setAuth(const String& urlPrefix, const String& token) {
    authPrefix = urlPrefix;
    authToken = token;
}

static bool wantsAuth(const String& url) {
    return authToken.length() && authPrefix.length() && url.startsWith(authPrefix);
}

void addAuth(esp_http_client_handle_t c, const String& url) {
    if (wantsAuth(url)) esp_http_client_set_header(c, kAuthHeader, authToken.c_str());
}

static bool isRedirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

esp_http_client_handle_t open(const String& url, int bufferSize, int timeoutMs, uint32_t rangeStart) {
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = timeoutMs;
    cfg.buffer_size = bufferSize;
    // GitHub's signed asset URLs run ~800 characters; the default 512-byte TX buffer
    // cannot hold the request line and fails with "Out of buffer".
    cfg.buffer_size_tx = 2048;
    cfg.user_agent = FW_NAME "/0";
    cfg.disable_auto_redirect = true;  // we do it by hand below
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return nullptr;
    addAuth(c, url);
    char range[40];
    if (rangeStart > 0) {
        snprintf(range, sizeof(range), "bytes=%u-", (unsigned)rangeStart);
        esp_http_client_set_header(c, "Range", range);
    }

    for (int hop = 0; hop <= kMaxRedirects; hop++) {
        if (esp_http_client_open(c, 0) != ESP_OK) {
            Serial.printf("[http] open failed (hop %d)\n", hop);
            break;
        }
        if (esp_http_client_fetch_headers(c) < 0) {
            Serial.printf("[http] no headers (hop %d)\n", hop);
            break;
        }
        int status = esp_http_client_get_status_code(c);
        if (status == 200 || (status == 206 && rangeStart > 0)) return c;
        if (!isRedirect(status)) {
            Serial.printf("[http] HTTP %d for %s\n", status, url.c_str());
            break;
        }
        // Drain the redirect body, then let the client swap in the Location header.
        char sink[256];
        while (esp_http_client_read(c, sink, sizeof(sink)) > 0) {}
        esp_http_client_close(c);
        if (esp_http_client_set_redirection(c) != ESP_OK) {
            Serial.println("[http] redirect without a usable Location");
            break;
        }
        char next[160];
        if (esp_http_client_get_url(c, next, sizeof(next)) != ESP_OK || !wantsAuth(String(next))) {
            esp_http_client_delete_header(c, kAuthHeader);   // the token stays with the resolver
        }
    }
    close(c);
    return nullptr;
}

void close(esp_http_client_handle_t c) {
    if (!c) return;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
}

uint32_t contentLength(esp_http_client_handle_t c) {
    int64_t len = esp_http_client_get_content_length(c);
    return len > 0 ? (uint32_t)len : 0;
}

bool text(const String& url, String& body, size_t maxBytes) {
    esp_http_client_handle_t c = open(url);
    if (!c) return false;
    char buf[512];
    int n;
    body = "";
    body.reserve(min(maxBytes, (size_t)8192));
    while ((n = esp_http_client_read(c, buf, sizeof(buf))) > 0 && body.length() < maxBytes) {
        body.concat(buf, n);
    }
    close(c);
    return body.length() > 0;
}

}  // namespace http_get
