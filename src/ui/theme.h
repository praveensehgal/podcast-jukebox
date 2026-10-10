#pragma once
#include <lvgl.h>

// Colours and metrics from docs/screens/podcast-jukebox-screens.html. Light theme only:
// the panel is read from a sofa, and a dark theme would halve its usable contrast.
namespace theme {

inline lv_color_t bg()        { return lv_color_hex(0xF7F5EF); }
inline lv_color_t header()    { return lv_color_hex(0x16213A); }
inline lv_color_t hilite()    { return lv_color_hex(0xB5431B); }
inline lv_color_t text()      { return lv_color_hex(0x141414); }
inline lv_color_t dim()       { return lv_color_hex(0x4A4A4A); }
inline lv_color_t rowline()   { return lv_color_hex(0xE3DFD5); }
inline lv_color_t sep()       { return lv_color_hex(0xECE7DC); }
inline lv_color_t white()     { return lv_color_hex(0xFFFFFF); }
inline lv_color_t green()     { return lv_color_hex(0x2F7D4F); }
inline lv_color_t amber()     { return lv_color_hex(0xB7791F); }
inline lv_color_t track()     { return lv_color_hex(0xD7D2C6); }

static const int kHeaderH = 36;
static const int kScrollbarW = 14;
static const int kRowH = 35;          // channel / settings rows: 8 visible
static const int kRowsVisible = 8;
static const int kEpisodeRowH = 71;   // episode rows: 4 visible
static const int kEpisodesVisible = 4;

inline const lv_font_t* fontRow()    { return &lv_font_montserrat_24; }
inline const lv_font_t* fontTitle()  { return &lv_font_montserrat_20; }
inline const lv_font_t* fontHeader() { return &lv_font_montserrat_16; }
inline const lv_font_t* fontSmall()  { return &lv_font_montserrat_14; }

}  // namespace theme
