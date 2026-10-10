// 06 Password: field with "Show", and a four-row keyboard on one LVGL button matrix.
// The last typed character stays visible for two seconds, then turns into a dot.

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static lv_obj_t* scr = nullptr;
static lv_obj_t* field = nullptr;
static lv_obj_t* kb = nullptr;
static String typed;
static bool reveal = false;
static bool shift = false;
static bool symbols = false;
static uint32_t lastTypedAt = 0;

static const char* kLower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                               "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                               LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
                               "123 #", " ", "Join", ""};
static const char* kUpper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                               "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                               LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
                               "123 #", " ", "Join", ""};
static const char* kSyms[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                              "-", "_", "@", "#", "$", "&", "*", "!", "?", "\n",
                              ".", ",", ":", ";", "/", "(", ")", "'", LV_SYMBOL_BACKSPACE, "\n",
                              "abc", " ", "Join", ""};

static void drawField() {
    String shown;
    for (size_t i = 0; i < typed.length(); i++) {
        bool last = i + 1 == typed.length() && millis() - lastTypedAt < 2000;
        shown += (reveal || last) ? String(typed[i]) : String("*");
    }
    lv_label_set_text(field, shown.length() ? shown.c_str() : " ");
}

static void applyMap() {
    lv_buttonmatrix_set_map(kb, symbols ? kSyms : (shift ? kUpper : kLower));
    // widths: shift and backspace 1.5, space 4, mode 1.5, Join 2 (relative units)
    int shiftId = 19, bsId = 27, modeId = 28, spaceId = 29, joinId = 30;
    lv_buttonmatrix_set_button_width(kb, shiftId, 3);
    lv_buttonmatrix_set_button_width(kb, bsId, 3);
    for (int i = 20; i < 27; i++) lv_buttonmatrix_set_button_width(kb, i, 2);
    lv_buttonmatrix_set_button_width(kb, modeId, 3);
    lv_buttonmatrix_set_button_width(kb, spaceId, 8);
    lv_buttonmatrix_set_button_width(kb, joinId, 4);
}

static void onKey(lv_event_t* e) {
    uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    const char* t = lv_buttonmatrix_get_button_text(kb, id);
    if (!t) return;
    if (!strcmp(t, "Join")) { input::push(input::Kind::Join); return; }
    if (!strcmp(t, LV_SYMBOL_BACKSPACE)) { if (typed.length()) typed.remove(typed.length() - 1); drawField(); return; }
    if (!strcmp(t, LV_SYMBOL_UP)) { shift = !shift; applyMap(); return; }
    if (!strcmp(t, "123 #")) { symbols = true; applyMap(); return; }
    if (!strcmp(t, "abc")) { symbols = false; applyMap(); return; }
    if (typed.length() < 63) { typed += t; lastTypedAt = millis(); }
    if (shift && !symbols) { shift = false; applyMap(); }
    drawField();
}

static void onShow(lv_event_t*) { reveal = !reveal; drawField(); }

void passwordShow(const char* ssid) {
    typed = "";
    reveal = shift = symbols = false;
    scr = widgets::screen();
    widgets::HeaderOpts o;
    o.back = true;
    widgets::header(scr, ssid, o);
    lv_obj_t* f = widgets::box(scr, 8, 56, theme::kW - 16, 44, theme::white());
    lv_obj_set_style_border_color(f, theme::header(), 0);
    lv_obj_set_style_border_width(f, 2, 0);
    lv_obj_set_style_radius(f, 8, 0);
    field = widgets::text(f, theme::fontRow(), theme::text(), " ");
    lv_obj_align(field, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t* show = widgets::box(f, theme::kW - 16 - 80, 0, 76, 40, theme::white());
    lv_obj_add_flag(show, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(show, onShow, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(widgets::text(show, theme::fontSmall(), theme::dim(), "Show"));

    kb = lv_buttonmatrix_create(scr);
    lv_obj_set_pos(kb, 4, 106);
    lv_obj_set_size(kb, theme::kW - 8, theme::kH - 110);
    lv_obj_set_style_pad_all(kb, 0, 0);
    lv_obj_set_style_pad_gap(kb, 5, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_bg_color(kb, theme::white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(0xD9D3C6), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, theme::fontTitle(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, theme::text(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, theme::pressed(), LV_PART_ITEMS | LV_STATE_PRESSED);
    applyMap();
    lv_obj_add_event_cb(kb, onKey, LV_EVENT_VALUE_CHANGED, nullptr);
    drawField();
    widgets::show(scr);
}

const String& passwordText() { return typed; }

void passwordTick() {
    static bool wasShowing = false;
    if (!scr || lv_screen_active() != scr) return;
    bool showing = millis() - lastTypedAt < 2000;
    if (wasShowing && !showing) drawField();
    wasShowing = showing;
}

}  // namespace ui
