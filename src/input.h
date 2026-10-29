#pragma once
#include <Arduino.h>

// One event stream for everything the user does: taps on the screen (pushed by the UI
// layer), the three buttons on top (play, pause, stop), and serial keys on the bench.
namespace input {

enum class Kind : uint8_t {
    None,
    Play, Pause, Stop, StopHold,          // top buttons; STOP held 3 s = Settings
    Pick,                                 // value = index of the tapped list row
    Back, Gear, NowPlaying,               // header taps
    VolDown, VolUp, SkipBack, SkipFwd,    // Now Playing taps
    Join,                                 // keyboard Join
    BenchPlay,                            // serial 't' on dev builds
};

struct Event {
    Kind kind;
    int16_t value;
};

// Top-button pins. They are the microSD socket's lines, which this design leaves unused:
// CLK 18, DAT0 19, CMD 23. Each four-prong tact switch goes between its pin and GND.
static const int kPinPlay = 18;
static const int kPinPause = 19;
static const int kPinStop = 23;

void begin();
bool poll(Event& e);                 // call from loop(); fills `e` when one is ready
void push(Kind k, int16_t value = 0);
void feedSerialKey(int c);   // P p s S t b g n - + < > j, 1-8 = list item

// A serial line that starts with '#' is a setup command, not keys: everything up to the
// newline (without the '#') goes to this handler, e.g. "token abc123" or "cfg".
void setCommandHandler(void (*handler)(const String& line));

}  // namespace input
