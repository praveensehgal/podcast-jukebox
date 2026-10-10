#pragma once
#include <Arduino.h>

// Physical controls, decision 001: SCROLL encoder (push = OK), VOLUME pot, and three
// buttons OK / PAUSE / STOP. Every source (encoder, buttons, pot, and the serial
// stand-in used until the parts arrive) reduces to this one event stream.
namespace input {

enum class Kind : uint8_t {
    None,
    Scroll,    // value = +n / -n detents
    Ok,
    Pause,
    Stop,
    StopHold,  // STOP held 3 s: Settings
    Volume,    // value = 0..16
};

struct Event {
    Kind kind;
    int16_t value;
};

// Configure pins (no-ops until the hardware is wired) and the serial stand-in.
void begin();

// Poll sources; call from loop(). Returns true and fills `e` when an event is ready.
bool poll(Event& e);

// Serial stand-in keys (115200): j/k or arrows = scroll, o or Enter = OK, p = pause,
// s = stop, S = stop-hold, +/- = volume up/down, 0..9 = volume 0..16 (scaled).
void feedSerialKey(int c);

}  // namespace input
