#include "input.h"

namespace input {

// ---- tiny ring queue ----
static const int kQueueLen = 16;
static Event queue[kQueueLen];
static volatile uint8_t qHead = 0, qTail = 0;

static void push(Kind k, int16_t v) {
    uint8_t next = (qHead + 1) % kQueueLen;
    if (next == qTail) return;  // drop on overflow; knobs are not critical
    queue[qHead] = {k, v};
    qHead = next;
}

static int16_t volume = 11;  // of 16, mockup default

// ---- serial stand-in ----
// Arrow keys arrive as ESC [ A/B; keep a 2-byte state to decode them.
static int escState = 0;

void feedSerialKey(int c) {
    if (escState == 1) { escState = (c == '[') ? 2 : 0; return; }
    if (escState == 2) {
        escState = 0;
        if (c == 'A') push(Kind::Scroll, -1);
        if (c == 'B') push(Kind::Scroll, +1);
        return;
    }
    switch (c) {
        case 27: escState = 1; return;
        case 'j': push(Kind::Scroll, +1); return;
        case 'k': push(Kind::Scroll, -1); return;
        case 'o': case '\r': case '\n': push(Kind::Ok, 0); return;
        case 'p': push(Kind::Pause, 0); return;
        case 's': push(Kind::Stop, 0); return;
        case 'S': push(Kind::StopHold, 0); return;
        case '+': case '=': volume = min<int16_t>(16, volume + 1); push(Kind::Volume, volume); return;
        case '-': volume = max<int16_t>(0, volume - 1); push(Kind::Volume, volume); return;
        default:
            if (c >= '0' && c <= '9') { volume = (c - '0') * 16 / 9; push(Kind::Volume, volume); }
            return;
    }
}

static void pollSerial() {
    while (Serial.available()) feedSerialKey(Serial.read());
}

// ---- hardware sources: placeholders until the encoder, pot and buttons are wired ----
// Planned pins (see docs/HANDOFF.md): encoder A/B 18/19 with internal pull-ups, push 23,
// OK 5, PAUSE 35 (external pull-up), STOP 0 (boot button), volume pot on 34 (ADC).
static void pollHardware() {}

void begin() {
    Serial.println("[input] serial stand-in active: j/k scroll, o OK, p pause, s stop, S hold, +/- volume");
}

bool poll(Event& e) {
    pollSerial();
    pollHardware();
    if (qHead == qTail) return false;
    e = queue[qTail];
    qTail = (qTail + 1) % kQueueLen;
    return true;
}

}  // namespace input
