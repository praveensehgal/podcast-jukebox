#include "audio_source.h"

#include <Arduino.h>

bool AudioSourceHttps::openAt(const char* url, uint32_t byteOffset) {
    close();
    url_ = url;
    client_ = http_get::open(url_, 4096, 20000, byteOffset);
    if (!client_) return false;
    uint32_t len = http_get::contentLength(client_);
    size_ = len ? len + byteOffset : 0;
    pos_ = byteOffset;
    ended_ = false;
    Serial.printf("[audio] stream open at %u, size %u\n", (unsigned)pos_, (unsigned)size_);
    return true;
}

uint32_t AudioSourceHttps::read(void* data, uint32_t len) {
    if (!client_) return 0;
    int n = esp_http_client_read(client_, (char*)data, len);
    if (n <= 0) {
        // 0 = server finished; <0 = error. Either way the decoder sees EOF.
        ended_ = true;
        return 0;
    }
    pos_ += n;
    return (uint32_t)n;
}

bool AudioSourceHttps::seek(int32_t pos, int dir) {
    uint32_t target;
    if (dir == SEEK_SET) target = (uint32_t)pos;
    else if (dir == SEEK_CUR) target = pos_ + pos;
    else if (dir == SEEK_END && size_) target = size_ + pos;
    else return false;
    if (size_ && target >= size_) return false;
    String u = url_;
    return openAt(u.c_str(), target);
}

bool AudioSourceHttps::close() {
    if (client_) http_get::close(client_);
    client_ = nullptr;
    return true;
}
