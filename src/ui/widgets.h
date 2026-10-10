#pragma once
#include <lvgl.h>

#include <functional>

#include "../input.h"

// Building blocks shared by every touch screen. Taps never run app logic directly:
// they push an input::Event, and the app handles it from loop(), outside LVGL's
// event processing. That keeps blocking work (HTTP, audio start) out of callbacks.
namespace widgets {

lv_obj_t* screen();
void show(lv_obj_t* scr);   // load, then delete the previous screen asynchronously

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t bg);   // not clickable
lv_obj_t* text(lv_obj_t* parent, const lv_font_t* f, lv_color_t c, const char* s);
// A tappable area that pushes `k` with `v`, with a pressed tint.
lv_obj_t* tap(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t bg, input::Kind k, int16_t v = 0);

struct HeaderOpts {
    bool back = false;
    bool gear = false;
    bool playing = false;          // green "Playing" pill, taps go to Now Playing
    const char* status = nullptr;  // right-aligned note, e.g. "Scanning..."
};
void header(lv_obj_t* scr, const char* title, const HeaderOpts& o);

// A list that pages with arrows on a right-hand rail. The screen supplies how to build
// a row's children once and how to fill them for a given item; the list does paging,
// hides empty rows, and turns a row tap into input::Kind::Pick with the item index.
struct Paged {
    int total = 0, page = 0, perPage = 4, rowH = 68;
    lv_obj_t* rows[4] = {};
    lv_obj_t* pg = nullptr;
    lv_obj_t* up = nullptr;
    lv_obj_t* down = nullptr;
    std::function<void(int slot, int index)> bind;
};
void paged(Paged& p, lv_obj_t* scr, int perPage, int rowH, const std::function<void(lv_obj_t*, int)>& make);
void pagedSet(Paged& p, int total, int page);
void pagedRedraw(Paged& p);
int pagedPages(const Paged& p);

// The common row: [art tile | dot | signal bars] name ........ [value | pill] [chevron]
struct SimpleRow {
    lv_obj_t* row = nullptr;
    lv_obj_t* art = nullptr;
    lv_obj_t* artLabel = nullptr;
    lv_obj_t* dot = nullptr;
    lv_obj_t* bars = nullptr;
    lv_obj_t* bar[4] = {};
    lv_obj_t* name = nullptr;
    lv_obj_t* value = nullptr;
    lv_obj_t* chev = nullptr;
};
struct RowData {
    const char* name = "";
    const char* value = nullptr;
    const char* art = nullptr;
    uint32_t artColor = 0;
    uint32_t dot = 0;
    int bars = -1;             // 0..4 signal bars, -1 none
    bool pill = false;         // draw value as a pill
    bool pillDark = false;
    uint32_t tint = 0;         // row background override, 0 = none
    bool muted = false;        // info row: dim text, no chevron
    const char* chev = LV_SYMBOL_RIGHT;
};
void makeSimpleRow(SimpleRow& r, lv_obj_t* row);
void bindSimpleRow(SimpleRow& r, const RowData& d);

}  // namespace widgets
