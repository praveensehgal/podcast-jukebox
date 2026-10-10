#pragma once
#include <lvgl.h>

// Sunton ESP32-3248S035R panel (ST7796, 480x320 landscape) driven by LovyanGFX,
// presented to LVGL 9 as a single display. Touch is deliberately not wired up:
// the device is knob-and-button only (decision 001).
namespace display {

static const int kWidth = 480;
static const int kHeight = 320;

// Bring up SPI, panel, backlight and LVGL. Call once from setup() before any UI.
void begin();

// Backlight 0..255. The boot path sets full brightness; Settings may dim later.
void setBrightness(uint8_t level);

// Pump LVGL. Call from loop() as often as possible; returns ms until next call.
uint32_t tick();

}  // namespace display
