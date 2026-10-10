// 03 Now Playing: art, title, time, state pill, progress, and the four touch buttons
// a hand reaches for: back 30 s, volume down, volume up, forward 30 s.

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static lv_obj_t* scr = nullptr;
static lv_obj_t* timeLbl = nullptr;
static lv_obj_t* pill = nullptr;
static lv_obj_t* pillText = nullptr;
static lv_obj_t* fill = nullptr;
static lv_obj_t* vol[8] = {};
static uint32_t lastPos = 0xFFFFFFFF;
static bool lastPaused = false;
static int lastVol = -1;
static const int kBarW = 448;

static void fmtTime(char* out, size_t n, uint32_t s) {
    if (s >= 3600) snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    else snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static lv_obj_t* control(lv_obj_t* parent, int x, int w, bool dark, const char* label, input::Kind k) {
    lv_obj_t* b = widgets::tap(parent, x, 0, w, 72, dark ? theme::header() : theme::rail(), k);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_center(widgets::text(b, dark ? theme::fontRow() : theme::fontTitle(), dark ? theme::white() : theme::header(), label));
    return b;
}

void playingShow(const char* channelName, const char* initials, uint32_t artColor, const resolver::Episode& e) {
    scr = widgets::screen();
    widgets::HeaderOpts o;
    o.back = true;
    widgets::header(scr, channelName, o);

    lv_obj_t* art = widgets::box(scr, 16, 60, 96, 96, lv_color_hex(artColor));
    lv_obj_set_style_radius(art, 12, 0);
    lv_obj_center(widgets::text(art, theme::fontRow(), theme::white(), initials));

    lv_obj_t* title = widgets::text(scr, theme::fontTitle(), theme::text(), e.title.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(title, 336, 52);
    lv_obj_set_pos(title, 128, 62);
    char meta[40];
    snprintf(meta, sizeof(meta), "%s  -  %u min", e.date.c_str(), (unsigned)((e.durationS + 30) / 60));
    lv_obj_set_pos(widgets::text(scr, theme::fontSmall(), theme::dim(), meta), 128, 124);

    timeLbl = widgets::text(scr, theme::fontRow(), theme::text(), "0:00 / --:--");
    lv_obj_set_pos(timeLbl, 16, 168);
    pill = widgets::box(scr, theme::kW - 16 - 112, 166, 112, 30, theme::green());
    lv_obj_set_style_radius(pill, 15, 0);
    pillText = widgets::text(pill, theme::fontMed(), theme::white(), "PLAYING");
    lv_obj_center(pillText);
    lv_obj_t* bar = widgets::box(scr, 16, 206, kBarW, 10, theme::track());
    lv_obj_set_style_radius(bar, 5, 0);
    fill = widgets::box(bar, 0, 0, 0, 10, theme::header());
    lv_obj_set_style_radius(fill, 5, 0);

    lv_obj_t* row = widgets::box(scr, 12, theme::kH - 12 - 72, theme::kW - 24, 72, theme::bg());
    control(row, 0, 88, false, LV_SYMBOL_LEFT " 30", input::Kind::SkipBack);
    control(row, 96, 88, true, LV_SYMBOL_MINUS, input::Kind::VolDown);
    for (int i = 0; i < 8; i++) {
        int h = 18 + i * 5;
        vol[i] = widgets::box(row, 192 + 6 + i * 9, 72 - 10 - h, 6, h, theme::track());
        lv_obj_set_style_radius(vol[i], 2, 0);
    }
    control(row, 272, 88, true, LV_SYMBOL_PLUS, input::Kind::VolUp);
    control(row, 368, 88, false, "30 " LV_SYMBOL_RIGHT, input::Kind::SkipFwd);

    lastPos = 0xFFFFFFFF;
    lastVol = -1;
    widgets::show(scr);
}

void playingUpdate(uint32_t posS, uint32_t durS, bool paused, uint8_t volume16) {
    if (!scr || lv_screen_active() != scr) return;
    if (posS != lastPos || paused != lastPaused) {
        lastPos = posS;
        lastPaused = paused;
        char a[16], b[16], t[40];
        fmtTime(a, sizeof(a), posS);
        fmtTime(b, sizeof(b), durS);
        snprintf(t, sizeof(t), "%s / %s", a, durS ? b : "--:--");
        lv_label_set_text(timeLbl, t);
        lv_label_set_text(pillText, paused ? "PAUSED" : "PLAYING");
        lv_obj_set_style_bg_color(pill, paused ? theme::amber() : theme::green(), 0);
        lv_obj_center(pillText);
        lv_obj_set_width(fill, durS ? (int)(kBarW * min(posS, durS) / durS) : 0);
    }
    int steps = (volume16 + 1) / 2;   // 0..16 -> 0..8 bars
    if (steps != lastVol) {
        lastVol = steps;
        for (int i = 0; i < 8; i++) lv_obj_set_style_bg_color(vol[i], i < steps ? theme::orange() : theme::track(), 0);
    }
}

}  // namespace ui
