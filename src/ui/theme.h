#pragma once
#include <lvgl.h>

// Colours and metrics from docs/screens/podcast-jukebox-touch-screens.html.
// Sizes are pixels on a 0.153 mm/px panel: 68 px is 10.4 mm, the smallest finger target.
namespace theme {

inline lv_color_t bg()        { return lv_color_hex(0xF7F5EF); }
inline lv_color_t header()    { return lv_color_hex(0x16213A); }
inline lv_color_t hilite()    { return lv_color_hex(0xB5431B); }
inline lv_color_t tint()      { return lv_color_hex(0xFBEEE8); }
inline lv_color_t pressed()   { return lv_color_hex(0xE9E3D6); }
inline lv_color_t text()      { return lv_color_hex(0x141414); }
inline lv_color_t dim()       { return lv_color_hex(0x4A4A4A); }
inline lv_color_t faint()     { return lv_color_hex(0xA39D90); }
inline lv_color_t rowline()   { return lv_color_hex(0xE3DFD5); }
inline lv_color_t rail()      { return lv_color_hex(0xECE7DC); }
inline lv_color_t white()     { return lv_color_hex(0xFFFFFF); }
inline lv_color_t green()     { return lv_color_hex(0x2F7D4F); }
inline lv_color_t amber()     { return lv_color_hex(0xB7791F); }
inline lv_color_t track()     { return lv_color_hex(0xD7D2C6); }
inline lv_color_t orange()    { return lv_color_hex(0xE2693A); }

static const int kW = 480, kH = 320;
static const int kHeaderH = 48;
static const int kRailW = 72;
static const int kListW = kW - kRailW;     // 408
static const int kBodyH = kH - kHeaderH;   // 272
static const int kRowH = 68;               // 4 rows per page
static const int kEpisodeRowH = 90;        // 3 rows per page (last one 92)

inline const lv_font_t* fontRow()    { return &lv_font_montserrat_24; }
inline const lv_font_t* fontTitle()  { return &lv_font_montserrat_20; }
inline const lv_font_t* fontMed()    { return &lv_font_montserrat_16; }
inline const lv_font_t* fontSmall()  { return &lv_font_montserrat_14; }

}  // namespace theme
