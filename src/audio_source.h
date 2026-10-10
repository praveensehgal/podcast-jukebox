#pragma once
#include <AudioFileSource.h>
#include <esp_http_client.h>

#include "http_get.h"

// ESP8266Audio source that streams over HTTPS through http_get (certificate bundle,
// redirect following, byte-range resume). The library's own HTTP source speaks plain
// HTTP only, and every podcast CDN is HTTPS.
class AudioSourceHttps : public AudioFileSource {
   public:
    AudioSourceHttps() {}
    ~AudioSourceHttps() override { close(); }

    bool open(const char* url) override { return openAt(url, 0); }
    bool openAt(const char* url, uint32_t byteOffset);
    uint32_t read(void* data, uint32_t len) override;
    uint32_t readNonBlock(void* data, uint32_t len) override { return read(data, len); }
    bool seek(int32_t pos, int dir) override;
    bool close() override;
    bool isOpen() override { return client_ != nullptr; }
    uint32_t getSize() override { return size_; }
    uint32_t getPos() override { return pos_; }

    // True once the server has closed the stream (end of episode).
    bool ended() const { return ended_; }

   private:
    esp_http_client_handle_t client_ = nullptr;
    String url_;
    uint32_t size_ = 0;   // full resource length (Content-Length + offset)
    uint32_t pos_ = 0;    // absolute byte position
    bool ended_ = false;
};
