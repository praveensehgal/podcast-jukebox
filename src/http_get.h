#pragma once
#include <Arduino.h>
#include <esp_http_client.h>

// Streaming HTTPS GET that follows redirects.
//
// esp_http_client only auto-follows 3xx inside esp_http_client_perform(). The
// open / fetch_headers / read streaming API, which we need to hash an image as it
// arrives, leaves redirect handling to the caller. GitHub release assets answer with
// a 302 to a signed ~800-character URL, so every fetch in this firmware goes through
// here. TLS trust is the ESP-IDF certificate bundle.
namespace http_get {

// Returns an open client positioned after the headers of a 200 response, or nullptr.
// The caller reads with esp_http_client_read() and must call close() afterwards.
esp_http_client_handle_t open(const String& url, int bufferSize = 2048, int timeoutMs = 15000);

void close(esp_http_client_handle_t c);

// Convenience: fetch a small text body (manifest, channel list). Caps at maxBytes.
bool text(const String& url, String& body, size_t maxBytes = 16 * 1024);

}  // namespace http_get
