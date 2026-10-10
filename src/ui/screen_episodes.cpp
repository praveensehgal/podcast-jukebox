// 02 Episodes: three a page. Date box, two-line title, badge, length, progress bar.

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

struct EpRow {
    lv_obj_t* mon;
    lv_obj_t* day;
    lv_obj_t* title;
    lv_obj_t* badge;
    lv_obj_t* dur;
    lv_obj_t* prog;
    lv_obj_t* fill;
};

static widgets::Paged lst;
static EpRow rows[3];
static const std::vector<resolver::Episode>* data = nullptr;
static String playing;

static void make(lv_obj_t* r, int i) {
    EpRow& e = rows[i];
    lv_obj_t* db = widgets::box(r, 0, 0, 72, theme::kEpisodeRowH - 1, theme::bg());
    lv_obj_set_style_bg_opa(db, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(db, theme::rowline(), 0);
    lv_obj_set_style_border_width(db, 1, 0);
    lv_obj_set_style_border_side(db, LV_BORDER_SIDE_RIGHT, 0);
    e.mon = widgets::text(db, theme::fontSmall(), theme::dim(), "");
    lv_obj_align(e.mon, LV_ALIGN_TOP_MID, 0, 16);
    e.day = widgets::text(db, theme::fontRow(), theme::text(), "");
    lv_obj_align(e.day, LV_ALIGN_BOTTOM_MID, 0, -16);
    e.title = widgets::text(r, theme::fontTitle(), theme::text(), "");
    lv_label_set_long_mode(e.title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(e.title, 312, 50);
    lv_obj_set_pos(e.title, 84, 8);
    e.badge = widgets::text(r, theme::fontSmall(), theme::dim(), "");
    lv_obj_set_style_pad_hor(e.badge, 7, 0);
    lv_obj_set_style_pad_ver(e.badge, 1, 0);
    lv_obj_set_style_radius(e.badge, 4, 0);
    lv_obj_align(e.badge, LV_ALIGN_BOTTOM_LEFT, 84, -10);
    e.dur = widgets::text(r, theme::fontSmall(), theme::dim(), "");
    lv_obj_align(e.dur, LV_ALIGN_BOTTOM_RIGHT, -12, -10);
    e.prog = widgets::box(r, 0, theme::kEpisodeRowH - 6, theme::kListW, 5, theme::track());
    e.fill = widgets::box(e.prog, 0, 0, 0, 5, theme::hilite());
}

static void bind(int slot, int index) {
    EpRow& r = rows[slot];
    const resolver::Episode& e = (*data)[index];
    int sp = e.date.indexOf(' ');
    lv_label_set_text(r.mon, sp > 0 ? e.date.substring(0, sp).c_str() : "");
    lv_label_set_text(r.day, sp > 0 ? e.date.substring(sp + 1).c_str() : "");
    lv_label_set_text(r.title, e.title.c_str());
    bool isPlaying = playing.length() && e.id == playing;
    uint32_t pct = (e.durationS && e.posS) ? min<uint32_t>(100, e.posS * 100 / e.durationS) : 0;
    char badge[24];
    bool isNew = !e.posS && !e.played && !isPlaying;
    if (isPlaying) snprintf(badge, sizeof(badge), "Playing - %u%%", (unsigned)pct);
    else if (e.played) strcpy(badge, "Played");
    else if (e.posS) snprintf(badge, sizeof(badge), "%u%% played", (unsigned)pct);
    else strcpy(badge, "NEW");
    lv_label_set_text(r.badge, badge);
    lv_obj_set_style_bg_opa(r.badge, isNew ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(r.badge, theme::green(), 0);
    lv_obj_set_style_text_color(r.badge, isNew ? theme::white() : (isPlaying ? theme::hilite() : theme::dim()), 0);
    char dur[16];
    snprintf(dur, sizeof(dur), "%u min", (unsigned)((e.durationS + 30) / 60));
    lv_label_set_text(r.dur, e.durationS ? dur : "");
    bool partial = e.posS && !e.played && e.durationS;
    partial ? lv_obj_remove_flag(r.prog, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(r.prog, LV_OBJ_FLAG_HIDDEN);
    if (partial) lv_obj_set_width(r.fill, theme::kListW * pct / 100);
}

void episodesShow(const char* channelName, const std::vector<resolver::Episode>& list, int page,
                  const String& playingId, bool isPlaying) {
    data = &list;
    playing = playingId;
    lv_obj_t* scr = widgets::screen();
    widgets::HeaderOpts o;
    o.back = true;
    o.playing = isPlaying;
    widgets::header(scr, channelName, o);
    lst = widgets::Paged();
    widgets::paged(lst, scr, 3, theme::kEpisodeRowH, make);
    lst.bind = bind;
    widgets::pagedSet(lst, (int)list.size(), page);
    if (list.empty()) {
        lv_obj_t* l = widgets::text(scr, theme::fontTitle(), theme::dim(), "No episodes right now.");
        lv_obj_align(l, LV_ALIGN_CENTER, -36, 20);
    }
    widgets::show(scr);
}

int episodesPage() { return lst.page; }

}  // namespace ui
