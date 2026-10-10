// 01 Channels (four a page) and the shared notice screen.

#include <cctype>

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static widgets::Paged lst;
static widgets::SimpleRow rows[4];
static const std::vector<channels::Channel>* data = nullptr;
static int playingIdx = -1;
static char artBuf[4][4];

static const uint32_t kPalette[] = {0x16213A, 0x7A1F1F, 0x2F5D8A, 0x2F7D4F, 0xB7791F, 0xB5431B, 0x5A3E8A, 0x1F1F1F};

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

static void bind(int slot, int index) {
    const channels::Channel& c = (*data)[index];
    widgets::RowData d;
    d.name = c.name.c_str();
    channelArt(c.name.c_str(), artBuf[slot], d.artColor);
    d.art = artBuf[slot];
    if (index == playingIdx) { d.tint = 0xFBEEE8; d.chev = LV_SYMBOL_AUDIO; }
    widgets::bindSimpleRow(rows[slot], d);
    if (index == playingIdx) lv_obj_set_style_text_color(rows[slot].chev, theme::hilite(), 0);
    else lv_obj_set_style_text_color(rows[slot].chev, theme::faint(), 0);
}

void channelsShow(const std::vector<channels::Channel>& list, int page, int playingIndex, bool playing) {
    data = &list;
    playingIdx = playingIndex;
    lv_obj_t* scr = widgets::screen();
    widgets::HeaderOpts o;
    o.gear = true;
    o.playing = playing;
    widgets::header(scr, "Channels", o);
    lst = widgets::Paged();
    widgets::paged(lst, scr, 4, theme::kRowH, [](lv_obj_t* r, int i) { widgets::makeSimpleRow(rows[i], r); });
    lst.bind = bind;
    widgets::pagedSet(lst, (int)list.size(), page);
    widgets::show(scr);
}

int channelsPage() { return lst.page; }

void noticeShow(const char* line1, const char* line2) {
    lv_obj_t* s = widgets::screen();
    lv_obj_t* a = widgets::text(s, theme::fontRow(), theme::text(), line1);
    lv_label_set_long_mode(a, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(a, 440);
    lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a, LV_ALIGN_CENTER, 0, -24);
    lv_obj_t* b = widgets::text(s, theme::fontTitle(), theme::dim(), line2 ? line2 : "");
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 36);
    widgets::show(s);
    lv_refr_now(nullptr);
}

}  // namespace ui
