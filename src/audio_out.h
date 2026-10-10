#pragma once
#include <Arduino.h>

// Audio playback: streams an episode URL (HTTPS, MP3 or AAC) to the MAX98357A over
// I2S. Pins are the Sunton's free P1-header lines plus one LED pin; the on-board
// speaker amp on GPIO 26 is not used because that line is analog into the amp.
namespace audio_out {

static const int kPinBclk = 22;
static const int kPinLrclk = 21;
static const int kPinDout = 16;   // green LED pin; the LED flickers with data, harmless

void begin();
// sizeBytes + totalSeconds (from the resolver) let resume seek by byte range.
bool play(const String& url, uint32_t resumeSeconds = 0, uint32_t sizeBytes = 0, uint32_t totalSeconds = 0);
void pause(bool paused);
bool isPaused();
void stop();
bool isPlaying();
void setVolume(uint8_t level16);   // 0..16, mapped to the decoder's 0..21
uint32_t positionSeconds();
uint32_t durationSeconds();         // 0 when the stream does not say
void loop();                        // call every few ms from the main loop

}  // namespace audio_out
