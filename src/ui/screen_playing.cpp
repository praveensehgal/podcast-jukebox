// Screen 03: Now Playing. Art tile, channel, title, elapsed / total, state pill,
// progress bar. The volume overlay (volume.cpp) slides over the bottom 58 px.

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static lv_obj_t* scr = nullptr;
static lv_obj_t* art = nullptr;
static lv_obj_t* artText = nullptr;
static lv_obj_t* chan = nullptr;
static lv_obj_t* title = nullptr;
static lv_obj_t* timeLbl = nullptr;
static lv_obj_t* pill = nullptr;
static lv_obj_t* pillText = nullptr;
static lv_obj_t* bar = nullptr;
static lv_obj_t* barFill = nullptr;
static uint32_t lastPos = 0xFFFFFFFF;
static bool lastPaused = false;

static lv_obj_t* plain(lv_obj_t* parent, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static void fmtTime(char* out, size_t n, uint32_t s) {
    if (s >= 3600) snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    else snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

void playingShow(const char* channelName, const char* initials, uint32_t artColor, const char* episodeTitle) {
    scr = widgets::screen();

    art = lv_obj_create(scr);
    lv_obj_remove_style_all(art);
    lv_obj_set_pos(art, 16, 14);
    lv_obj_set_size(art, 120, 120);
    lv_obj_set_style_radius(art, 8, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(artColor), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    artText = plain(art, theme::fontRow(), theme::white());
    lv_label_set_text(artText, initials);
    lv_obj_center(artText);

    chan = plain(scr, theme::fontSmall(), theme::dim());
    lv_label_set_text(chan, channelName);
    lv_obj_set_pos(chan, 152, 14);

    title = plain(scr, theme::fontTitle(), theme::text());
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, 312);
    lv_obj_set_pos(title, 152, 38);
    lv_label_set_text(title, episodeTitle);

    timeLbl = plain(scr, theme::fontRow(), theme::text());
    lv_obj_set_pos(timeLbl, 16, 150);
    lv_label_set_text(timeLbl, "0:00 / 0:00");

    pill = lv_obj_create(scr);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 110, 30);
    lv_obj_set_pos(pill, 480 - 16 - 110, 150);
    lv_obj_set_style_radius(pill, 15, 0);
    lv_obj_set_style_bg_color(pill, theme::green(), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    pillText = plain(pill, theme::fontHeader(), theme::white());
    lv_label_set_text(pillText, "PLAYING");
    lv_obj_center(pillText);

    bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 16, 196);
    lv_obj_set_size(bar, 448, 14);
    lv_obj_set_style_radius(bar, 7, 0);
    lv_obj_set_style_bg_color(bar, theme::track(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    barFill = lv_obj_create(bar);
    lv_obj_remove_style_all(barFill);
    lv_obj_set_size(barFill, 0, 14);
    lv_obj_set_style_radius(barFill, 7, 0);
    lv_obj_set_style_bg_color(barFill, theme::header(), 0);
    lv_obj_set_style_bg_opa(barFill, LV_OPA_COVER, 0);

    lastPos = 0xFFFFFFFF;
    widgets::show(scr);
}

void playingUpdate(uint32_t posS, uint32_t durS, bool paused) {
    if (!scr || lv_screen_active() != scr) return;
    if (posS == lastPos && paused == lastPaused) return;
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
    lv_obj_set_width(barFill, durS ? (int)(448UL * min(posS, durS) / durS) : 0);
}

}  // namespace ui
