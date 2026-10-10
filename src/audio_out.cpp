// ESP8266Audio wrapper: Helix MP3 decoder only (the whole-library alternative with
// every codec linked was 870 KB, 560 KB past the OTA slot). Stream -> 16 KB ring
// buffer -> decoder -> I2S to the MAX98357A.

#include "audio_out.h"

#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3a.h>
#include <AudioOutputI2S.h>

#include "audio_source.h"

#include <esp_heap_caps.h>

#include <new>

namespace audio_out {

// 16 KB = 1 s at 128 kbps. The HTTP client holds another 4 KB and downloads run well
// ahead of real time on Wi-Fi; 24 KB tipped the heap into bad_alloc at play start.
static const size_t kBufferBytes = 16 * 1024;
// Heap budget on this no-PSRAM board, measured 2026-10-10 (byte-addressable heap; the
// 40 KB "largest block" ESP.getMaxAllocHeap() reports while playing is instruction
// RAM that buffers cannot use):
//   free before a stream opens  ~105 KB      MP3 decoder (Helix tables)  ~34 KB
//   a TLS session to a CDN      56-75 KB     this ring                    16 KB
// TLS plus decoder plus ring does not fit, so audio comes over plain HTTP from the
// resolver (decision 005). The ring is taken after the stream opens, with malloc and
// a step-down, never new(): new() threw bad_alloc and aborted. A static array is out
// too: the extra .bss shrank the biggest heap region from 73 KB to 41 KB.
static const size_t kMinBufferBytes = 6 * 1024;
static uint8_t* ringMem = nullptr;
static size_t ringBytes = 0;

static void heapLine(const char* tag) {
    Serial.printf("[audio] %s: 8bit free %u largest %u | internal largest %u\n", tag,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)ESP.getMaxAllocHeap());
}

static bool takeRing() {
    for (size_t n = kBufferBytes; n >= kMinBufferBytes; n -= 2048) {
        ringMem = (uint8_t*)malloc(n);
        if (ringMem) { ringBytes = n; return true; }
    }
    return false;
}

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

// Reuse-safe decoder: the library's begin() leaves the previous stream's leftovers.
class ReusableMP3 : public AudioGeneratorMP3a {
   public:
    bool ready() const { return hMP3Decoder != nullptr; }
};

static AudioSourceHttps* source = nullptr;
static AudioFileSourceBuffer* buffer = nullptr;
static ReusableMP3* decoder = nullptr;   // about 31 KB with Helix's tables
static bool loaded = false;              // a stream is attached to the decoder
static PersistentI2S* out = nullptr;

static bool paused = false;
static uint32_t durationS = 0;   // from the resolver, used for resume + time display
static uint32_t sizeBytes_ = 0;  // kept so seekTo can reopen the same stream
static String url_;
static uint32_t startOffsetS = 0;
static uint32_t startedMs = 0;
static uint32_t pausedAccumMs = 0;
static uint32_t pausedAtMs = 0;

void begin() {
    out = new PersistentI2S();
    out->SetPinout(kPinBclk, kPinLrclk, kPinDout);
    out->SetGain(0.6f);
    bool ok = out->open();
    Serial.printf("[audio] I2S %s: bclk %d lrclk %d dout %d, free heap %u largest %u\n",
                  ok ? "up" : "FAILED", kPinBclk, kPinLrclk, kPinDout,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

static void teardown() {
    if (decoder) { if (decoder->isRunning()) decoder->stop(); delete decoder; decoder = nullptr; }
    loaded = false;
    if (buffer) { buffer->close(); delete buffer; buffer = nullptr; }
    if (source) { source->close(); delete source; source = nullptr; }
    free(ringMem);   // the buffer object never owns it (pre-allocated constructor)
    ringMem = nullptr;
    ringBytes = 0;
}

bool play(const String& url, uint32_t resumeSeconds, uint32_t sizeBytes, uint32_t totalSeconds) {
    teardown();
    durationS = totalSeconds;
    sizeBytes_ = sizeBytes;
    url_ = url;
    // Byte-range resume assumes constant bitrate, which is true of nearly every
    // podcast MP3. Off by a few seconds at worst; he hears a familiar sentence.
    uint32_t offset = (resumeSeconds && sizeBytes && totalSeconds)
                          ? (uint32_t)((uint64_t)sizeBytes * resumeSeconds / totalSeconds) : 0;
    heapLine("before open");
    source = new (std::nothrow) AudioSourceHttps();
    if (!source) return false;
    if (!source->openAt(url.c_str(), offset)) {
        Serial.printf("[audio] FAILED to open %s\n", url.c_str());
        teardown();
        return false;
    }
    heapLine("stream open");
    // Decoder first (fixed 31 KB), then the ring, which can shrink to what is left.
    decoder = new (std::nothrow) ReusableMP3();
    if (!decoder || !decoder->ready()) { Serial.println("[audio] no room for the decoder"); teardown(); return false; }
    if (!takeRing()) { Serial.println("[audio] no room for the ring buffer"); teardown(); return false; }
    buffer = new (std::nothrow) AudioFileSourceBuffer(source, ringMem, ringBytes);
    if (!buffer) { Serial.println("[audio] out of memory at play start"); teardown(); return false; }
    if (!decoder->begin(buffer, out)) {
        Serial.println("[audio] decoder refused the stream");
        teardown();
        return false;
    }
    loaded = true;
    paused = false;
    startOffsetS = resumeSeconds;
    startedMs = millis();
    pausedAccumMs = 0;
    Serial.printf("[audio] playing from %u s, ring %u KB, heap %u largest %u\n", (unsigned)resumeSeconds,
                  (unsigned)(ringBytes / 1024), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return true;
}

void pause(bool p) {
    if (!loaded || p == paused) return;
    paused = p;
    if (p) pausedAtMs = millis(); else pausedAccumMs += millis() - pausedAtMs;
}

bool isPaused() { return paused; }

bool seekTo(uint32_t seconds) {
    if (!loaded || !sizeBytes_ || !durationS) return false;   // byte offset needs both
    if (seconds >= durationS) seconds = durationS > 5 ? durationS - 5 : 0;
    bool wasPaused = paused;
    String url = url_;   // play() overwrites url_; keep our own copy
    if (!play(url, seconds, sizeBytes_, durationS)) return false;
    if (wasPaused) pause(true);
    return true;
}

void stop() {
    teardown();
    paused = false;
    url_ = "";
}

bool isPlaying() { return loaded && decoder->isRunning(); }

void setVolume(uint8_t level16) {
    if (out) out->SetGain(level16 / 16.0f);
}

uint32_t positionSeconds() {
    if (!loaded) return 0;
    uint32_t played = millis() - startedMs - pausedAccumMs - (paused ? millis() - pausedAtMs : 0);
    return startOffsetS + played / 1000;
}

uint32_t durationSeconds() { return durationS; }

void loop() {
    if (!loaded || paused) return;
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
