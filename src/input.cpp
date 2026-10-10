#include "input.h"

namespace input {

static const int kQueueLen = 16;
static Event queue[kQueueLen];
static uint8_t qHead = 0, qTail = 0;

void push(Kind k, int16_t v) {
    uint8_t next = (qHead + 1) % kQueueLen;
    if (next == qTail) return;
    queue[qHead] = {k, v};
    qHead = next;
}

// ---- top buttons: active low with pull-ups, 25 ms debounce, STOP hold for Settings ----
struct Button {
    int pin;
    Kind kind;
    bool down = false;
    bool raw = false;
    uint32_t rawSince = 0;
    uint32_t downSince = 0;
    bool holdFired = false;
};
static Button buttons[] = {{kPinPlay, Kind::Play}, {kPinPause, Kind::Pause}, {kPinStop, Kind::Stop}};
static const uint32_t kDebounceMs = 25;
static const uint32_t kHoldMs = 3000;

static void pollButton(Button& b, uint32_t now) {
    bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.raw) { b.raw = raw; b.rawSince = now; return; }
    if (now - b.rawSince < kDebounceMs || raw == b.down) {
        if (b.down && b.kind == Kind::Stop && !b.holdFired && now - b.downSince >= kHoldMs) {
            b.holdFired = true;
            push(Kind::StopHold);
        }
        return;
    }
    b.down = raw;
    if (raw) { b.downSince = now; b.holdFired = false; if (b.kind != Kind::Stop) push(b.kind); }
    else if (b.kind == Kind::Stop && !b.holdFired) push(Kind::Stop);   // STOP acts on release so a hold can win
}

// ---- serial stand-ins for the bench ----
void feedSerialKey(int c) {
    switch (c) {
        case 'P': push(Kind::Play); break;
        case 'p': push(Kind::Pause); break;
        case 's': push(Kind::Stop); break;
        case 'S': push(Kind::StopHold); break;
        case 't': push(Kind::BenchPlay); break;
        case 'b': push(Kind::Back); break;
        case 'g': push(Kind::Gear); break;
        case 'n': push(Kind::NowPlaying); break;
        case '-': push(Kind::VolDown); break;
        case '+': push(Kind::VolUp); break;
        case '<': push(Kind::SkipBack); break;
        case '>': push(Kind::SkipFwd); break;
        case 'j': push(Kind::Join); break;
        default:
            if (c >= '1' && c <= '8') push(Kind::Pick, c - '1');   // item 1..8 of the list on screen
            break;
    }
}

void begin() {
    for (auto& b : buttons) pinMode(b.pin, INPUT_PULLUP);
    Serial.printf("[input] buttons play %d pause %d stop %d; serial: P p s S t b g n - + < > j 1-8\n", kPinPlay, kPinPause, kPinStop);
}

bool poll(Event& e) {
    while (Serial.available()) feedSerialKey(Serial.read());
    uint32_t now = millis();
    for (auto& b : buttons) pollButton(b, now);
    if (qHead == qTail) return false;
    e = queue[qTail];
    qTail = (qTail + 1) % kQueueLen;
    return true;
}

}  // namespace input
