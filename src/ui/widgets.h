#pragma once
#include <lvgl.h>

#include <functional>

// Shared building blocks for every screen: the dark header bar, the right-hand
// scrollbar, and a virtual list that keeps a fixed set of row widgets and rebinds
// them as the selection moves. Virtual rows matter here: 20 channels times a
// handful of labels each would eat the 40 KB LVGL heap on a board with no PSRAM.
namespace widgets {

// Header: title left, optional count right ("7 of 20"), optional back glyph.
struct Header {
    lv_obj_t* bar;
    lv_obj_t* title;
    lv_obj_t* count;
};
Header header(lv_obj_t* parent, const char* title, bool back);
void headerCount(Header& h, int current, int total);  // current <= 0 clears
void headerText(Header& h, const char* text);          // free text on the right

// Scrollbar strip under the header; thumb size/position derive from the list.
struct Scrollbar {
    lv_obj_t* track;
    lv_obj_t* thumb;
};
Scrollbar scrollbar(lv_obj_t* parent);
void scrollbarSet(Scrollbar& s, int first, int visible, int total);

// A row as the list wants to draw it. `art` is up to 3 initials on a coloured square
// when artColor != 0; `dot` draws a status dot in that colour when != 0.
struct RowData {
    const char* name = "";
    const char* value = nullptr;   // right-aligned secondary text (settings screens)
    const char* art = nullptr;
    uint32_t artColor = 0;
    uint32_t dot = 0;
    bool separator = false;        // dimmer, non-selectable style
};

struct List {
    lv_obj_t* box = nullptr;
    lv_obj_t* rows[8];
    lv_obj_t* arts[8];
    lv_obj_t* artLabels[8];
    lv_obj_t* dots[8];
    lv_obj_t* names[8];
    lv_obj_t* values[8];
    int total = 0;
    int selected = 0;
    int first = 0;
    int visible = 8;
    std::function<void(int, RowData&)> provider;
    Scrollbar sb;
    Header* hdr = nullptr;
};

// Creates the list below the header; `visible` <= 8.
void list(List& l, lv_obj_t* parent, int visible, Header* hdr);
void listSetTotal(List& l, int total);
void listSelect(List& l, int index);   // clamps, scrolls the window, redraws
void listMove(List& l, int delta);     // wraps at the ends (decision 001)
void listRedraw(List& l);

}  // namespace widgets
