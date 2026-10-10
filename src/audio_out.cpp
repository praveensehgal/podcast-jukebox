// ESP8266Audio wrapper: Helix MP3 decoder only (the whole-library alternative with
// every codec linked was 870 KB, 560 KB past the OTA slot). Stream -> 24 KB ring
// buffer -> decoder -> I2S to the MAX98357A.

#include "audio_out.h"

#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3a.h>
#include <AudioOutputI2S.h>

#include "audio_source.h"

namespace audio_out {

// 16 KB = 1 s at 128 kbps. The HTTP client holds another 4 KB and downloads run well
// ahead of real time on Wi-Fi; 24 KB tipped the heap into bad_alloc at play start.
static const size_t kBufferBytes = 16 * 1024;

// The stock output allocates the I2S channel and its DMA descriptors inside
// begin(), which the decoder calls at play time, after TLS, the ring buffer and the
// decoder itself have fragmented the heap. On this no-PSRAM board that allocation
// failed. This subclass opens the channel once at boot and keeps it; stop() just
// flushes silence so the DMA ring does not loop the last buffer.
class PersistentI2S : public AudioOutputI2S {
   public:
    PersistentI2S() : AudioOutputI2S(4, 512) {}  // 4 x 512 B DMA buffers, plenty at 44.1 kHz
    bool open() { opened_ = AudioOutputI2S::begin(); return opened_; }
    bool begin() override { return opened_; }
    bool stop() override { if (opened_) flush(); return true; }
   private:
    bool opened_ = false;
};

static AudioSourceHttps* source = nullptr;
static AudioFileSourceBuffer* buffer = nullptr;
static AudioGeneratorMP3a* decoder = nullptr;
static PersistentI2S* out = nullptr;

static bool paused = false;
static uint32_t durationS = 0;   // from the resolver, used for resume + time display
static uint32_t startOffsetS = 0;
static uint32_t startedMs = 0;
static uint32_t pausedAccumMs = 0;
static uint32_t pausedAtMs = 0;

void begin() {
    out = new PersistentI2S();
    out->SetPinout(kPinBclk, kPinLrclk, kPinDout);
    out->SetGain(0.6f);
    bool ok = out->open();
    Serial.printf("[audio] I2S %s: bclk %d lrclk %d dout %d, free heap %u\n",
                  ok ? "up" : "FAILED", kPinBclk, kPinLrclk, kPinDout, (unsigned)ESP.getFreeHeap());
}

static void teardown() {
    if (decoder) { if (decoder->isRunning()) decoder->stop(); delete decoder; decoder = nullptr; }
    if (buffer) { buffer->close(); delete buffer; buffer = nullptr; }
    if (source) { source->close(); delete source; source = nullptr; }
}

bool play(const String& url, uint32_t resumeSeconds, uint32_t sizeBytes, uint32_t totalSeconds) {
    teardown();
    durationS = totalSeconds;
    // Byte-range resume assumes constant bitrate, which is true of nearly every
    // podcast MP3. Off by a few seconds at worst; he hears a familiar sentence.
    uint32_t offset = (resumeSeconds && sizeBytes && totalSeconds)
                          ? (uint32_t)((uint64_t)sizeBytes * resumeSeconds / totalSeconds) : 0;
    source = new AudioSourceHttps();
    if (!source->openAt(url.c_str(), offset)) {
        Serial.printf("[audio] FAILED to open %s\n", url.c_str());
        teardown();
        return false;
    }
    buffer = new AudioFileSourceBuffer(source, kBufferBytes);
    decoder = new AudioGeneratorMP3a();
    if (!decoder->begin(buffer, out)) {
        Serial.println("[audio] decoder refused the stream");
        teardown();
        return false;
    }
    paused = false;
    startOffsetS = resumeSeconds;
    startedMs = millis();
    pausedAccumMs = 0;
    Serial.printf("[audio] playing from %u s, free heap %u\n", (unsigned)resumeSeconds, (unsigned)ESP.getFreeHeap());
    return true;
}

void pause(bool p) {
    if (!decoder || p == paused) return;
    paused = p;
    if (p) pausedAtMs = millis(); else pausedAccumMs += millis() - pausedAtMs;
}

bool isPaused() { return paused; }

void stop() {
    teardown();
    paused = false;
}

bool isPlaying() { return decoder && decoder->isRunning(); }

void setVolume(uint8_t level16) {
    if (out) out->SetGain(level16 / 16.0f);
}

uint32_t positionSeconds() {
    if (!decoder) return 0;
    uint32_t played = millis() - startedMs - pausedAccumMs - (paused ? millis() - pausedAtMs : 0);
    return startOffsetS + played / 1000;
}

uint32_t durationSeconds() { return durationS; }

void loop() {
    if (!decoder || paused) return;
    if (decoder->isRunning()) {
        if (!decoder->loop()) {
            decoder->stop();
            Serial.println("[audio] ended");
            return;
        }
        static uint32_t lastReport = 0;
        if (millis() - lastReport > 5000) {
            lastReport = millis();
            Serial.printf("[audio] %u s  pos %u/%u bytes  heap %u  largest %u\n",
                          (unsigned)positionSeconds(), (unsigned)source->getPos(), (unsigned)source->getSize(),
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        }
    }
}

}  // namespace audio_out
