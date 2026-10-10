// Transient volume overlay: slides over the bottom 58 px of whatever screen is
// active while the knob moves, hides 2 s after the last click (mockup screen 03).

#include "theme.h"
#include "ui.h"

namespace ui {

static lv_obj_t* panel = nullptr;
static lv_obj_t* segs[16];
static lv_obj_t* pct = nullptr;
static uint32_t hideAt = 0;

static void build() {
    panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 480, 58);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(panel, theme::header(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

    lv_obj_t* t = lv_label_create(panel);
    lv_obj_set_style_text_font(t, theme::fontHeader(), 0);
    lv_obj_set_style_text_color(t, theme::white(), 0);
    lv_label_set_text(t, "VOLUME");
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);

    for (int i = 0; i < 16; i++) {
        segs[i] = lv_obj_create(panel);
        lv_obj_remove_style_all(segs[i]);
        lv_obj_set_size(segs[i], 14, 26);
        lv_obj_set_pos(segs[i], 100 + i * 18, 16);
        lv_obj_set_style_radius(segs[i], 2, 0);
        lv_obj_set_style_bg_opa(segs[i], LV_OPA_COVER, 0);
    }
    pct = lv_label_create(panel);
    lv_obj_set_style_text_font(pct, theme::fontHeader(), 0);
    lv_obj_set_style_text_color(pct, theme::white(), 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

void volumeFlash(int level) {
    if (!panel) build();
    for (int i = 0; i < 16; i++)
        lv_obj_set_style_bg_color(segs[i], i < level ? lv_color_hex(0xE2693A) : lv_color_hex(0x4A5570), 0);
    lv_label_set_text_fmt(pct, "%d/16", level);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    hideAt = millis() + 2000;
}

void volumeTick() {
    if (panel && hideAt && millis() > hideAt) {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
        hideAt = 0;
    }
}

}  // namespace ui
