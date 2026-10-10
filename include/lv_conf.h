/* LVGL 9 configuration for the Podcast Jukebox.
 * Only overrides live here; everything else takes lv_conf_internal.h defaults.
 * Board has no PSRAM, so the LVGL heap and draw buffer are kept small. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Memory: 40 KB internal heap for widgets. Draw buffer is allocated in display.cpp. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (40 * 1024U)

/* Software renderer: only the formats this panel path touches. Each disabled
 * format drops its blend routines from flash (several KB apiece). */
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE

#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 166

/* Logging off in production builds; flip to 1 + LV_LOG_LEVEL_WARN when debugging. */
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* Fonts: the four sizes the mockup uses. ASCII + Latin-1 only. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* Widgets actually used. Everything else off to save flash. */
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_LINE 1
#define LV_USE_QRCODE 1
#define LV_USE_ARC 0
#define LV_USE_BUTTON 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 1   /* required by LV_USE_QRCODE */
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 1    /* required by LV_USE_CANVAS */
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_LOTTIE 0
#define LV_USE_ANIMIMG 0

#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* No image decoders, no file systems, no OS. */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_LODEPNG 0
#define LV_USE_TJPGD 0
#define LV_USE_GIF 0
#define LV_USE_BARCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_OS LV_OS_NONE
#define LV_USE_DEMO_WIDGETS 0
#define LV_BUILD_EXAMPLES 0

#endif /* LV_CONF_H */
