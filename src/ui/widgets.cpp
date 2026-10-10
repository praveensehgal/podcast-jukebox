#include "widgets.h"

#include <cstdio>

#include "theme.h"

namespace widgets {

// ---------- screens ----------

lv_obj_t* screen() {
    lv_obj_t* s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, theme::bg(), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

void show(lv_obj_t* scr) {
    lv_obj_t* old = lv_screen_active();
    lv_screen_load(scr);
    // Async: the tap that triggered this may still be running on the old screen.
    if (old && old != scr) lv_obj_delete_async(old);
}

// ---------- primitives ----------

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t bg) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* text(lv_obj_t* parent, const lv_font_t* f, lv_color_t c, const char* s) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, s ? s : "");
    return l;
}

static void onTap(lv_event_t* e) {
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(e);
    input::push((input::Kind)(packed & 0xFF), (int16_t)(packed >> 8));
}

lv_obj_t* tap(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t bg, input::Kind k, int16_t v) {
    lv_obj_t* o = box(parent, x, y, w, h, bg);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_80, LV_STATE_PRESSED);
    uintptr_t packed = (uintptr_t)(uint8_t)k | ((uintptr_t)(uint16_t)v << 8);
    lv_obj_add_event_cb(o, onTap, LV_EVENT_CLICKED, (void*)packed);
    return o;
}

// ---------- header ----------

void header(lv_obj_t* scr, const char* title, const HeaderOpts& o) {
    lv_obj_t* bar = box(scr, 0, 0, theme::kW, theme::kHeaderH, theme::header());
    int x = 14;
    if (o.back) {
        lv_obj_t* b = tap(bar, 0, 0, 56, theme::kHeaderH, theme::header(), input::Kind::Back);
        lv_obj_center(text(b, theme::fontRow(), theme::white(), LV_SYMBOL_LEFT));
        x = 60;
    }
    int right = theme::kW;
    if (o.gear) {
        right -= 56;
        lv_obj_t* g = tap(bar, right, 0, 56, theme::kHeaderH, theme::header(), input::Kind::Gear);
        lv_obj_center(text(g, theme::fontRow(), theme::white(), LV_SYMBOL_SETTINGS));
    }
    if (o.playing) {
        right -= 124;
        lv_obj_t* p = tap(bar, right, 8, 114, 32, theme::green(), input::Kind::NowPlaying);
        lv_obj_set_style_radius(p, 16, 0);
        lv_obj_center(text(p, theme::fontSmall(), theme::white(), LV_SYMBOL_AUDIO "  Playing"));
    }
    if (o.status) {
        lv_obj_t* s = text(bar, theme::fontSmall(), theme::white(), o.status);
        lv_obj_set_style_text_opa(s, LV_OPA_80, 0);
        lv_obj_align(s, LV_ALIGN_RIGHT_MID, -(theme::kW - right) - 12, 0);
    }
    lv_obj_t* t = text(bar, theme::fontTitle(), theme::white(), title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, right - x - 12);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
}

// ---------- paged list ----------

static void onRow(lv_event_t* e) {
    Paged* p = (Paged*)lv_event_get_user_data(e);
    lv_obj_t* row = (lv_obj_t*)lv_event_get_current_target(e);
    int slot = (int)(intptr_t)lv_obj_get_user_data(row);
    input::push(input::Kind::Pick, (int16_t)(p->page * p->perPage + slot));
}

static void onUp(lv_event_t* e) {
    Paged* p = (Paged*)lv_event_get_user_data(e);
    if (p->page > 0) { p->page--; pagedRedraw(*p); }
}

static void onDown(lv_event_t* e) {
    Paged* p = (Paged*)lv_event_get_user_data(e);
    if (p->page < pagedPages(*p) - 1) { p->page++; pagedRedraw(*p); }
}

static lv_obj_t* arrow(lv_obj_t* rail, int y, const char* sym, lv_event_cb_t cb, Paged* p) {
    lv_obj_t* a = box(rail, 0, y, theme::kRailW, 112, theme::rail());
    lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(a, theme::pressed(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(a, cb, LV_EVENT_CLICKED, p);
    lv_obj_center(text(a, theme::fontRow(), theme::header(), sym));
    return a;
}

void paged(Paged& p, lv_obj_t* scr, int perPage, int rowH, const std::function<void(lv_obj_t*, int)>& make) {
    p.perPage = perPage;
    p.rowH = rowH;
    for (int i = 0; i < perPage; i++) {
        int h = (i == perPage - 1) ? theme::kBodyH - rowH * (perPage - 1) : rowH;
        lv_obj_t* r = box(scr, 0, theme::kHeaderH + i * rowH, theme::kListW, h, theme::bg());
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(r, theme::pressed(), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(r, theme::rowline(), 0);
        lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_user_data(r, (void*)(intptr_t)i);
        lv_obj_add_event_cb(r, onRow, LV_EVENT_CLICKED, &p);
        p.rows[i] = r;
        make(r, i);
    }
    lv_obj_t* rail = box(scr, theme::kListW, theme::kHeaderH, theme::kRailW, theme::kBodyH, theme::rail());
    lv_obj_set_style_border_color(rail, lv_color_hex(0xD9D3C6), 0);
    lv_obj_set_style_border_width(rail, 1, 0);
    lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_LEFT, 0);
    p.up = arrow(rail, 0, LV_SYMBOL_UP, onUp, &p);
    p.down = arrow(rail, theme::kBodyH - 112, LV_SYMBOL_DOWN, onDown, &p);
    p.pg = text(rail, theme::fontMed(), theme::dim(), "");
    lv_obj_align(p.pg, LV_ALIGN_CENTER, 0, 0);
}

int pagedPages(const Paged& p) { return p.total <= 0 ? 1 : (p.total + p.perPage - 1) / p.perPage; }

void pagedSet(Paged& p, int total, int page) {
    p.total = total;
    p.page = page < 0 ? 0 : page;
    if (p.page >= pagedPages(p)) p.page = pagedPages(p) - 1;
    pagedRedraw(p);
}

void pagedRedraw(Paged& p) {
    for (int i = 0; i < p.perPage; i++) {
        int index = p.page * p.perPage + i;
        if (index >= p.total) { lv_obj_add_flag(p.rows[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(p.rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(p.rows[i], theme::bg(), 0);
        if (p.bind) p.bind(i, index);
    }
    int pages = pagedPages(p);
    lv_label_set_text_fmt(p.pg, "%d / %d", p.page + 1, pages);
    lv_obj_align(p.pg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(p.up, 0), p.page > 0 ? theme::header() : theme::faint(), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(p.down, 0), p.page < pages - 1 ? theme::header() : theme::faint(), 0);
}

// ---------- simple row ----------

void makeSimpleRow(SimpleRow& r, lv_obj_t* row) {
    r.row = row;
    r.art = box(row, 14, 12, 44, 44, theme::header());
    lv_obj_set_style_radius(r.art, 8, 0);
    r.artLabel = text(r.art, theme::fontSmall(), theme::white(), "");
    lv_obj_center(r.artLabel);
    r.dot = box(row, 16, 28, 12, 12, theme::green());
    lv_obj_set_style_radius(r.dot, 6, 0);
    r.bars = box(row, 14, 18, 30, 30, theme::bg());
    lv_obj_set_style_bg_opa(r.bars, LV_OPA_TRANSP, 0);
    for (int i = 0; i < 4; i++) r.bar[i] = box(r.bars, i * 8, 24 - i * 7, 5, 6 + i * 7, theme::header());
    r.name = text(row, theme::fontRow(), theme::text(), "");
    lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
    r.value = text(row, theme::fontMed(), theme::dim(), "");
    lv_obj_set_style_pad_hor(r.value, 12, 0);
    lv_obj_set_style_pad_ver(r.value, 6, 0);
    lv_obj_set_style_radius(r.value, 16, 0);
    r.chev = text(row, theme::fontTitle(), theme::faint(), LV_SYMBOL_RIGHT);
}

static void showIf(lv_obj_t* o, bool on) { on ? lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

void bindSimpleRow(SimpleRow& r, const RowData& d) {
    if (d.tint) lv_obj_set_style_bg_color(r.row, lv_color_hex(d.tint), 0);
    int x = 14;
    bool art = d.art && d.artColor;
    showIf(r.art, art);
    if (art) { lv_obj_set_style_bg_color(r.art, lv_color_hex(d.artColor), 0); lv_label_set_text(r.artLabel, d.art); lv_obj_center(r.artLabel); x = 72; }
    showIf(r.dot, d.dot != 0);
    if (d.dot) { lv_obj_set_style_bg_color(r.dot, lv_color_hex(d.dot), 0); x = 40; }
    showIf(r.bars, d.bars >= 0);
    if (d.bars >= 0) { for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(r.bar[i], i < d.bars ? theme::header() : theme::track(), 0); x = 56; }
    lv_label_set_text(r.name, d.name);
    lv_obj_set_style_text_color(r.name, d.muted ? theme::dim() : theme::text(), 0);
    lv_obj_set_style_text_font(r.name, d.muted ? theme::fontMed() : theme::fontRow(), 0);
    bool chev = !d.muted && d.chev && d.chev[0];
    showIf(r.chev, chev);
    if (chev) { lv_label_set_text(r.chev, d.chev); lv_obj_align(r.chev, LV_ALIGN_RIGHT_MID, -14, 0); }
    int valueRight = chev ? -40 : -14;
    showIf(r.value, d.value != nullptr);
    int valueW = 0;
    if (d.value) {
        lv_label_set_text(r.value, d.value);
        lv_obj_set_style_bg_opa(r.value, d.pill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(r.value, d.pillDark ? theme::header() : theme::rail(), 0);
        lv_obj_set_style_text_color(r.value, d.pill && d.pillDark ? theme::white() : theme::dim(), 0);
        lv_obj_align(r.value, LV_ALIGN_RIGHT_MID, valueRight, 0);
        lv_obj_update_layout(r.value);
        valueW = lv_obj_get_width(r.value) + 8;
    }
    lv_obj_set_width(r.name, theme::kListW - x - (-valueRight) - valueW - 6);
    lv_obj_align(r.name, LV_ALIGN_LEFT_MID, x, 0);
}

}  // namespace widgets
