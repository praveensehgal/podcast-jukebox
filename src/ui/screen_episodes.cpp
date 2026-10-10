// Screen 02: episode list. 4 rows of 71 px: date box left, two-line title, NEW badge
// or "n% played" / "Played", duration right, thin progress bar on partly played.

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static const int kRows = theme::kEpisodesVisible;
static const int kRowH = theme::kEpisodeRowH;

static lv_obj_t* scr = nullptr;
static widgets::Header hdr;
static widgets::Scrollbar sb;
static lv_obj_t* rows[kRows];
static lv_obj_t* dateMon[kRows];
static lv_obj_t* dateDay[kRows];
static lv_obj_t* titles[kRows];
static lv_obj_t* badges[kRows];
static lv_obj_t* durs[kRows];
static lv_obj_t* progs[kRows];
static lv_obj_t* progFill[kRows];
static const std::vector<resolver::Episode>* data = nullptr;
static int selected = 0, first = 0;

static lv_obj_t* plain(lv_obj_t* parent, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static void makeRow(int i) {
    lv_obj_t* r = lv_obj_create(scr);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, 0, theme::kHeaderH + i * kRowH);
    lv_obj_set_size(r, 480 - theme::kScrollbarW, kRowH);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r, theme::rowline(), 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    rows[i] = r;

    lv_obj_t* db = lv_obj_create(r);
    lv_obj_remove_style_all(db);
    lv_obj_set_pos(db, 0, 0);
    lv_obj_set_size(db, 74, kRowH - 1);
    lv_obj_set_style_border_color(db, theme::rowline(), 0);
    lv_obj_set_style_border_width(db, 1, 0);
    lv_obj_set_style_border_side(db, LV_BORDER_SIDE_RIGHT, 0);
    dateMon[i] = plain(db, theme::fontSmall(), theme::dim());
    lv_obj_align(dateMon[i], LV_ALIGN_TOP_MID, 0, 12);
    dateDay[i] = plain(db, theme::fontRow(), theme::text());
    lv_obj_align(dateDay[i], LV_ALIGN_BOTTOM_MID, 0, -8);

    titles[i] = plain(r, theme::fontHeader(), theme::text());
    lv_label_set_long_mode(titles[i], LV_LABEL_LONG_WRAP);
    lv_obj_set_width(titles[i], 380);
    lv_obj_set_pos(titles[i], 84, 5);

    badges[i] = plain(r, theme::fontSmall(), theme::dim());
    lv_obj_align(badges[i], LV_ALIGN_BOTTOM_LEFT, 84, -8);
    durs[i] = plain(r, theme::fontSmall(), theme::dim());
    lv_obj_align(durs[i], LV_ALIGN_BOTTOM_RIGHT, -10, -8);

    progs[i] = lv_obj_create(r);
    lv_obj_remove_style_all(progs[i]);
    lv_obj_set_size(progs[i], 480 - theme::kScrollbarW, 4);
    lv_obj_align(progs[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(progs[i], theme::track(), 0);
    lv_obj_set_style_bg_opa(progs[i], LV_OPA_COVER, 0);
    progFill[i] = lv_obj_create(progs[i]);
    lv_obj_remove_style_all(progFill[i]);
    lv_obj_set_size(progFill[i], 0, 4);
    lv_obj_set_style_bg_color(progFill[i], theme::hilite(), 0);
    lv_obj_set_style_bg_opa(progFill[i], LV_OPA_COVER, 0);
}

static void bind(int i, int index) {
    lv_obj_t* r = rows[i];
    if (index >= (int)data->size()) { lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(r, LV_OBJ_FLAG_HIDDEN);
    const resolver::Episode& e = (*data)[index];
    bool sel = index == selected;
    lv_color_t fg = sel ? theme::white() : theme::text();
    lv_color_t fgDim = sel ? theme::white() : theme::dim();
    lv_obj_set_style_bg_color(r, sel ? theme::hilite() : theme::bg(), 0);
    int sp = e.date.indexOf(' ');
    lv_label_set_text(dateMon[i], sp > 0 ? e.date.substring(0, sp).c_str() : "");
    lv_label_set_text(dateDay[i], sp > 0 ? e.date.substring(sp + 1).c_str() : "");
    lv_obj_set_style_text_color(dateMon[i], fgDim, 0);
    lv_obj_set_style_text_color(dateDay[i], fg, 0);
    lv_label_set_text(titles[i], e.title.c_str());
    lv_obj_set_style_text_color(titles[i], fg, 0);
    char badge[24];
    uint32_t pct = (e.durationS && e.posS) ? (e.posS * 100 / e.durationS) : 0;
    if (e.played) strcpy(badge, "Played");
    else if (e.posS) snprintf(badge, sizeof(badge), "%u%% played", (unsigned)pct);
    else strcpy(badge, "NEW");
    lv_label_set_text(badges[i], badge);
    lv_obj_set_style_text_color(badges[i], (!e.posS && !e.played) ? (sel ? theme::white() : theme::green()) : fgDim, 0);
    char dur[16];
    snprintf(dur, sizeof(dur), "%u min", (unsigned)((e.durationS + 30) / 60));
    lv_label_set_text(durs[i], e.durationS ? dur : "");
    lv_obj_set_style_text_color(durs[i], fgDim, 0);
    bool partial = e.posS && !e.played && e.durationS;
    partial ? lv_obj_remove_flag(progs[i], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(progs[i], LV_OBJ_FLAG_HIDDEN);
    if (partial) lv_obj_set_width(progFill[i], (480 - theme::kScrollbarW) * pct / 100);
}

static void redraw() {
    for (int i = 0; i < kRows; i++) bind(i, first + i);
    widgets::scrollbarSet(sb, first, kRows, (int)data->size());
    widgets::headerCount(hdr, data->empty() ? 0 : selected + 1, (int)data->size());
}

void episodesShow(const char* channelName, const std::vector<resolver::Episode>& list, int sel) {
    data = &list;
    selected = list.empty() ? 0 : max(0, min(sel, (int)list.size() - 1));
    first = max(0, min(selected, (int)list.size() - kRows));
    scr = widgets::screen();
    hdr = widgets::header(scr, channelName, true);
    for (int i = 0; i < kRows; i++) makeRow(i);
    sb = widgets::scrollbar(scr);
    if (list.empty()) {
        lv_obj_t* l = plain(scr, theme::fontTitle(), theme::dim());
        lv_label_set_text(l, "No episodes available right now.");
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 10);
    }
    redraw();
    widgets::show(scr);
}

bool episodesHandle(const input::Event& e) {
    if (e.kind != input::Kind::Scroll || data->empty()) return false;
    int n = (int)data->size();
    selected = ((selected + e.value) % n + n) % n;
    if (selected < first) first = selected;
    if (selected >= first + kRows) first = selected - kRows + 1;
    redraw();
    return true;
}

int episodesSelected() { return selected; }

}  // namespace ui
