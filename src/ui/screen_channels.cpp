// Screen 01: channel list. 8 rows of 24 px type, whole-row highlight, "n of 20".

#include <cstring>

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static lv_obj_t* scr = nullptr;
static widgets::Header hdr;
static widgets::List lst;
static const std::vector<channels::Channel>* data = nullptr;
static char artBuf[8][4];

// Art colour from the name so each channel keeps a stable, distinct tile.
static const uint32_t kPalette[] = {0x16213A, 0x7A1F1F, 0x2F5D8A, 0x2F7D4F, 0xB7791F,
                                    0xB5431B, 0x5A3E8A, 0x1F1F1F};

void channelArt(const char* name, char* out, uint32_t& color) {
    int n = 0;
    bool word = true;
    for (const char* p = name; *p && n < 3; p++) {
        if (*p == ' ') { word = true; continue; }
        if (word && isalnum((unsigned char)*p)) out[n++] = toupper((unsigned char)*p);
        word = false;
    }
    out[n] = '\0';
    uint32_t h = 0;
    for (const char* p = name; *p; p++) h = h * 31 + (unsigned char)*p;
    color = kPalette[h % 8];
}

static void provide(int index, widgets::RowData& d) {
    if (index >= (int)data->size()) { d.name = "Settings"; d.separator = true; return; }
    const channels::Channel& c = (*data)[index];
    d.name = c.name.c_str();
    int slot = index % 8;
    channelArt(c.name.c_str(), artBuf[slot], d.artColor);
    d.art = artBuf[slot];
}

void noticeShow(const char* line1, const char* line2) {
    lv_obj_t* s = widgets::screen();
    lv_obj_t* a = lv_label_create(s);
    lv_obj_set_style_text_font(a, theme::fontRow(), 0);
    lv_obj_set_style_text_color(a, theme::text(), 0);
    lv_label_set_text(a, line1);
    lv_obj_align(a, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t* b = lv_label_create(s);
    lv_obj_set_style_text_font(b, theme::fontTitle(), 0);
    lv_obj_set_style_text_color(b, theme::dim(), 0);
    lv_label_set_text(b, line2 ? line2 : "");
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 24);
    widgets::show(s);
    lv_refr_now(nullptr);
}

void channelsShow(const std::vector<channels::Channel>& list, int selected) {
    data = &list;
    scr = widgets::screen();
    hdr = widgets::header(scr, "CHANNELS", false);
    widgets::list(lst, scr, theme::kRowsVisible, &hdr);
    lst.provider = provide;
    lst.selected = selected;
    lst.countTotal = (int)list.size();
    widgets::listSetTotal(lst, (int)list.size() + 1);  // + "Settings" row past the last channel
    widgets::show(scr);
}

bool channelsHandle(const input::Event& e) {
    if (e.kind == input::Kind::Scroll) { widgets::listMove(lst, e.value); return true; }
    return false;
}

int channelsSelected() { return lst.selected; }

}  // namespace ui
