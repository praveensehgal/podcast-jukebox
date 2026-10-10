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

static void initials(const char* name, char* out) {
    int n = 0;
    bool word = true;
    for (const char* p = name; *p && n < 3; p++) {
        if (*p == ' ') { word = true; continue; }
        if (word && isalnum((unsigned char)*p)) out[n++] = toupper((unsigned char)*p);
        word = false;
    }
    out[n] = '\0';
}

static void provide(int index, widgets::RowData& d) {
    const channels::Channel& c = (*data)[index];
    d.name = c.name.c_str();
    int slot = index % 8;
    initials(c.name.c_str(), artBuf[slot]);
    d.art = artBuf[slot];
    uint32_t h = 0;
    for (const char* p = c.name.c_str(); *p; p++) h = h * 31 + (unsigned char)*p;
    d.artColor = kPalette[h % 8];
}

void channelsShow(const std::vector<channels::Channel>& list, int selected) {
    data = &list;
    scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, theme::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    hdr = widgets::header(scr, "CHANNELS", false);
    widgets::list(lst, scr, theme::kRowsVisible, &hdr);
    lst.provider = provide;
    lst.selected = selected;
    widgets::listSetTotal(lst, (int)list.size());
    lv_screen_load(scr);
}

bool channelsHandle(const input::Event& e) {
    if (e.kind == input::Kind::Scroll) { widgets::listMove(lst, e.value); return true; }
    return false;
}

int channelsSelected() { return lst.selected; }

}  // namespace ui
