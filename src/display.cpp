// LovyanGFX board definition + LVGL 9 glue for the Sunton ESP32-3248S035R.
// Pin map and the invert=false finding come from the esp32-mac-macro-pad project,
// which runs on the same board (verified on hardware 2026-08-22).

#include "display.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>

namespace display {

class Panel : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

   public:
    Panel() {
        {
            auto cfg = _bus.config();
            cfg.spi_host = HSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_dc = 2;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 15;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.panel_width = 320;   // native portrait; rotated to landscape in begin()
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = false;      // true renders every colour as its negative on this panel
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;   // XPT2046 shares the bus even though we never talk to it
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 27;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

static Panel tft;
static lv_display_t* disp = nullptr;
static const int kBufLines = 40;  // 480 * 40 * 2 bytes = 37.5 KB, single buffer, DRAM
static uint16_t* drawBuf = nullptr;

static void flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((lgfx::rgb565_t*)px, w * h);
    tft.endWrite();
    lv_display_flush_ready(d);
}

static uint32_t tickMs() { return millis(); }

void begin() {
    // Keep the untouched XPT2046 deselected so it never drives MISO during panel writes.
    pinMode(33, OUTPUT);
    digitalWrite(33, HIGH);

    tft.init();
    tft.setRotation(1);  // landscape, USB on the right
    tft.setBrightness(255);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(tickMs);
    drawBuf = (uint16_t*)heap_caps_malloc(kWidth * kBufLines * sizeof(uint16_t), MALLOC_CAP_DMA);
    disp = lv_display_create(kWidth, kHeight);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, drawBuf, nullptr, kWidth * kBufLines * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);
    Serial.printf("[display] ST7796 up, draw buffer %u bytes, free heap %u\n",
                  (unsigned)(kWidth * kBufLines * 2), (unsigned)ESP.getFreeHeap());
}

void setBrightness(uint8_t level) { tft.setBrightness(level); }

uint32_t tick() { return lv_timer_handler(); }

}  // namespace display
