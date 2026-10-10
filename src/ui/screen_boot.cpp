// Boot screen: shown from power-on until the channel list is ready. It doubles as
// the only place a non-technical owner will ever see what the box is doing, so the
// lines are plain words, not log tags.

#include <cstring>

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static lv_obj_t* scr = nullptr;
static lv_obj_t* status[6];
static int statusCount = 0;

void bootShow(const char* version) {
    scr = widgets::screen();

    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 480, 110);
    lv_obj_set_style_bg_color(bar, theme::header(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_obj_t* t = lv_label_create(bar);
    lv_obj_set_style_text_font(t, theme::fontRow(), 0);
    lv_obj_set_style_text_color(t, theme::white(), 0);
    lv_label_set_text(t, "Podcast Jukebox");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 20, 26);

    lv_obj_t* v = lv_label_create(bar);
    lv_obj_set_style_text_font(v, theme::fontMed(), 0);
    lv_obj_set_style_text_color(v, theme::white(), 0);
    lv_obj_set_style_text_opa(v, LV_OPA_70, 0);
    lv_label_set_text_fmt(v, "version %s", version);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, 20, 64);

    for (int i = 0; i < 6; i++) {
        status[i] = lv_label_create(scr);
        lv_obj_set_style_text_font(status[i], theme::fontTitle(), 0);
        lv_obj_set_style_text_color(status[i], i == 5 ? theme::text() : theme::dim(), 0);
        lv_label_set_long_mode(status[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(status[i], 440);
        lv_obj_align(status[i], LV_ALIGN_TOP_LEFT, 20, 128 + i * 30);
        lv_label_set_text(status[i], "");
    }
    statusCount = 0;
    widgets::show(scr);
    lv_refr_now(nullptr);
}

void bootStatus(const char* line) {
    if (!scr || lv_screen_active() != scr) return;
    if (statusCount < 6) {
        lv_label_set_text(status[statusCount++], line);
    } else {
        for (int i = 0; i < 5; i++) lv_label_set_text(status[i], lv_label_get_text(status[i + 1]));
        lv_label_set_text(status[5], line);
    }
    // Newest line in full ink, older ones dimmer, like a receipt tail.
    for (int i = 0; i < 6; i++)
        lv_obj_set_style_text_color(status[i], i == statusCount - 1 ? theme::text() : theme::dim(), 0);
    lv_refr_now(nullptr);
}

}  // namespace ui
