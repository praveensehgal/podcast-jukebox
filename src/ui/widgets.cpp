#include "widgets.h"

#include <algorithm>
#include <cstdio>

#include "theme.h"

namespace widgets {

using std::max;
using std::min;

static lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t bg) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return l;
}

// ---------- screens ----------

lv_obj_t* screen() {
    lv_obj_t* s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, theme::bg(), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

void show(lv_obj_t* scr) {
    lv_obj_t* old = lv_screen_active();
    lv_screen_load(scr);
    if (old && old != scr) lv_obj_delete(old);
}

// ---------- header ----------

Header header(lv_obj_t* parent, const char* title, bool back) {
    Header h;
    h.bar = box(parent, 0, 0, 480, theme::kHeaderH, theme::header());
    h.title = label(h.bar, theme::fontHeader(), theme::white());
    char buf[48];
    snprintf(buf, sizeof(buf), "%s%s", back ? LV_SYMBOL_LEFT "  " : "", title);
    lv_label_set_text(h.title, buf);
    lv_obj_align(h.title, LV_ALIGN_LEFT_MID, 12, 0);
    h.count = label(h.bar, theme::fontHeader(), theme::white());
    lv_obj_set_style_text_opa(h.count, LV_OPA_80, 0);
    lv_label_set_text(h.count, "");
    lv_obj_align(h.count, LV_ALIGN_RIGHT_MID, -12, 0);
    return h;
}

void headerCount(Header& h, int current, int total) {
    char buf[24];
    if (current <= 0) buf[0] = '\0'; else snprintf(buf, sizeof(buf), "%d of %d", current, total);
    lv_label_set_text(h.count, buf);
    lv_obj_align(h.count, LV_ALIGN_RIGHT_MID, -12, 0);
}

void headerText(Header& h, const char* text) {
    lv_label_set_text(h.count, text);
    lv_obj_align(h.count, LV_ALIGN_RIGHT_MID, -12, 0);
}

// ---------- scrollbar ----------

Scrollbar scrollbar(lv_obj_t* parent) {
    Scrollbar s;
    s.track = box(parent, 480 - theme::kScrollbarW, theme::kHeaderH, theme::kScrollbarW,
                  320 - theme::kHeaderH, theme::rowline());
    s.thumb = box(s.track, 3, 0, theme::kScrollbarW - 6, 40, theme::header());
    lv_obj_set_style_radius(s.thumb, 4, 0);
    return s;
}

void scrollbarSet(Scrollbar& s, int first, int visible, int total) {
    int h = 320 - theme::kHeaderH;
    if (total <= visible) { lv_obj_set_pos(s.thumb, 3, 0); lv_obj_set_height(s.thumb, h); return; }
    int th = max(24, h * visible / total);
    int ty = (h - th) * first / (total - visible);
    lv_obj_set_pos(s.thumb, 3, ty);
    lv_obj_set_height(s.thumb, th);
}

// ---------- virtual list ----------

static void makeRow(List& l, int i) {
    lv_obj_t* r = box(l.box, 0, i * theme::kRowH, 480 - theme::kScrollbarW, theme::kRowH, theme::bg());
    lv_obj_set_style_border_color(r, theme::rowline(), 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    l.rows[i] = r;
    l.arts[i] = box(r, 10, 4, 26, 26, theme::header());
    lv_obj_set_style_radius(l.arts[i], 4, 0);
    l.artLabels[i] = label(l.arts[i], theme::fontSmall(), theme::white());
    lv_obj_center(l.artLabels[i]);
    l.dots[i] = box(r, 12, 12, 10, 10, theme::green());
    lv_obj_set_style_radius(l.dots[i], 5, 0);
    l.names[i] = label(r, theme::fontRow(), theme::text());
    lv_obj_set_width(l.names[i], 300);
    l.values[i] = label(r, theme::fontHeader(), theme::dim());
    lv_obj_align(l.values[i], LV_ALIGN_RIGHT_MID, -10, 0);
}

void list(List& l, lv_obj_t* parent, int visible, Header* hdr) {
    l.visible = min(visible, 8);
    l.hdr = hdr;
    l.box = box(parent, 0, theme::kHeaderH, 480 - theme::kScrollbarW, 320 - theme::kHeaderH, theme::bg());
    for (int i = 0; i < l.visible; i++) makeRow(l, i);
    l.sb = scrollbar(parent);
}

void listSetTotal(List& l, int total) {
    l.total = total;
    if (l.selected >= total) l.selected = max(0, total - 1);
    listSelect(l, l.selected);
}

static void bindRow(List& l, int i, int index) {
    lv_obj_t* r = l.rows[i];
    if (index >= l.total) { lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(r, LV_OBJ_FLAG_HIDDEN);
    RowData d;
    if (l.provider) l.provider(index, d);
    bool sel = (index == l.selected) && !d.separator;
    lv_obj_set_style_bg_color(r, sel ? theme::hilite() : (d.separator ? theme::sep() : theme::bg()), 0);
    lv_color_t fg = sel ? theme::white() : (d.separator ? theme::dim() : theme::text());
    lv_obj_set_style_text_color(l.names[i], fg, 0);
    lv_obj_set_style_text_font(l.names[i], d.separator ? theme::fontTitle() : theme::fontRow(), 0);
    lv_label_set_text(l.names[i], d.name);
    int x = 10;
    bool art = d.art && d.artColor;
    if (art) {
        lv_obj_set_style_bg_color(l.arts[i], lv_color_hex(d.artColor), 0);
        lv_label_set_text(l.artLabels[i], d.art);
        x = 46;
    }
    art ? lv_obj_remove_flag(l.arts[i], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(l.arts[i], LV_OBJ_FLAG_HIDDEN);
    if (d.dot) { lv_obj_set_style_bg_color(l.dots[i], lv_color_hex(d.dot), 0); x = 30; }
    d.dot ? lv_obj_remove_flag(l.dots[i], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(l.dots[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(l.names[i], LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_set_style_text_color(l.values[i], sel ? theme::white() : theme::dim(), 0);
    lv_label_set_text(l.values[i], d.value ? d.value : "");
    lv_obj_align(l.values[i], LV_ALIGN_RIGHT_MID, -10, 0);
}

void listRedraw(List& l) {
    for (int i = 0; i < l.visible; i++) bindRow(l, i, l.first + i);
    scrollbarSet(l.sb, l.first, l.visible, l.total);
    if (l.hdr) headerCount(*l.hdr, (l.total && l.selected < l.countTotal) ? l.selected + 1 : 0, l.countTotal);
}

void listSelect(List& l, int index) {
    if (l.total == 0) { l.selected = 0; l.first = 0; listRedraw(l); return; }
    l.selected = (index % l.total + l.total) % l.total;
    if (l.selected < l.first) l.first = l.selected;
    if (l.selected >= l.first + l.visible) l.first = l.selected - l.visible + 1;
    l.first = max(0, min(l.first, max(0, l.total - l.visible)));
    listRedraw(l);
}

void listMove(List& l, int delta) { listSelect(l, l.selected + delta); }

}  // namespace widgets
