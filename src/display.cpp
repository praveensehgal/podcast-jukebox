// LovyanGFX board definition + LVGL 9 glue for the Sunton ESP32-3248S035R.
// Pin map, invert=false and the XPT2046 calibration come from the esp32-mac-macro-pad
// project, which ran on this exact board (board 1, verified 2026-08-22).

#include "display.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>

namespace display {

class Panel : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_XPT2046 _touch;

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
            cfg.bus_shared = true;   // XPT2046 shares this SPI bus
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
        {   // resistive touch on the same HSPI bus; raw range is a placeholder, the
            // per-panel corner calibration below is what maps it to the screen
            auto cfg = _touch.config();
            cfg.x_min = 300; cfg.x_max = 3900; cfg.y_min = 200; cfg.y_max = 3800;
            cfg.pin_int = 36;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host = HSPI_HOST;
            cfg.freq = 1000000;
            cfg.pin_sclk = 14; cfg.pin_mosi = 13; cfg.pin_miso = 12; cfg.pin_cs = 33;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

static Panel tft;
static lv_display_t* disp = nullptr;
// 480 * 10 * 2 bytes = 9.4 KB, single buffer, DRAM. It was 40 lines (37.5 KB) until
// 2026-10-10: an open TLS audio stream measured 56 KB of byte-addressable heap, which
// left 21 KB for a ring buffer and MP3 decoder that need about 40. The screen repaints
// in more, smaller strips; the heap goes to audio.
static const int kBufLines = 10;
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

// Corners measured on this panel with the macro pad's touch_cal tool, setRotation(1).
// Order: raw (x, y) at top-left, top-right, bottom-left, bottom-right.
static uint16_t kTouchCal[8] = {3578, 3688, 3588, 361, 491, 3710, 474, 376};

static void touchRead(lv_indev_t*, lv_indev_data_t* data) {
    static bool wasDown = false;
    uint16_t x, y;
    bool down = tft.getTouch(&x, &y);
    if (down) {
        data->point.x = x;
        data->point.y = y;
        if (!wasDown) Serial.printf("[touch] %u,%u\n", x, y);
    }
    data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    wasDown = down;
}

void begin() {
    tft.init();
    tft.setRotation(1);  // landscape, USB on the right
    tft.setTouchCalibrate(kTouchCal);
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
    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touchRead);
    Serial.printf("[display] ST7796 up, draw buffer %u bytes, free heap %u\n",
                  (unsigned)(kWidth * kBufLines * 2), (unsigned)ESP.getFreeHeap());
}

void setBrightness(uint8_t level) { tft.setBrightness(level); }

uint32_t tick() { return lv_timer_handler(); }

}  // namespace display
