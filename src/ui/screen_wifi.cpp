// Screens 05 and 06: Wi-Fi network list with the phone QR route, and the knob
// password strip. Scanning is synchronous (2-3 s) behind a notice.

#include <WiFi.h>

#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

// ---------- Wi-Fi list ----------

static lv_obj_t* scr = nullptr;
static widgets::Header hdr;
static widgets::List lst;
static std::vector<String> ssids;
static std::vector<int> rssis;
static std::vector<bool> locked;
static String rowVal[8];
static lv_obj_t* qr = nullptr;

static void provideWifi(int i, widgets::RowData& d) {
    if (i < (int)ssids.size()) {
        d.name = ssids[i].c_str();
        int r = rssis[i];
        rowVal[i % 8] = r > -60 ? "Strong" : (r > -72 ? "Good" : "Weak");
        if (locked[i]) rowVal[i % 8] += "  " LV_SYMBOL_WIFI;
        d.value = rowVal[i % 8].c_str();
    } else {
        d.name = "Type a network name";
        d.separator = true;
    }
}

void wifiScan() {
    ssids.clear(); rssis.clear(); locked.clear();
    int n = WiFi.scanNetworks(false, false, false, 300);
    for (int i = 0; i < n && ssids.size() < 12; i++) {
        String s = WiFi.SSID(i);
        if (!s.length()) continue;
        bool dup = false;
        for (auto& x : ssids) if (x == s) dup = true;
        if (dup) continue;
        ssids.push_back(s);
        rssis.push_back(WiFi.RSSI(i));
        locked.push_back(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    Serial.printf("[wifi] %u networks\n", (unsigned)ssids.size());
}

void wifiShow(const char* setupSsid, const char* setupKey) {
    scr = widgets::screen();
    hdr = widgets::header(scr, "WI-FI", true);
    widgets::list(lst, scr, 8, nullptr);
    // Narrow the list to leave room for the QR panel on the right.
    lv_obj_set_width(lst.box, 296);
    for (int i = 0; i < 8; i++) { lv_obj_set_width(lst.rows[i], 296); lv_obj_set_width(lst.names[i], 200); }
    lst.provider = provideWifi;
    widgets::listSetTotal(lst, (int)ssids.size() + 1);

    lv_obj_t* side = lv_obj_create(scr);
    lv_obj_remove_style_all(side);
    lv_obj_set_pos(side, 296, theme::kHeaderH);
    lv_obj_set_size(side, 480 - 296 - theme::kScrollbarW, 320 - theme::kHeaderH);
    lv_obj_set_style_border_color(side, theme::rowline(), 0);
    lv_obj_set_style_border_width(side, 1, 0);
    lv_obj_set_style_border_side(side, LV_BORDER_SIDE_LEFT, 0);

    String payload = String("WIFI:T:WPA;S:") + setupSsid + ";P:" + setupKey + ";;";
    qr = lv_qrcode_create(side);
    lv_qrcode_set_size(qr, 150);
    lv_qrcode_set_dark_color(qr, theme::text());
    lv_qrcode_set_light_color(qr, theme::white());
    lv_qrcode_update(qr, payload.c_str(), payload.length());
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* cap = lv_label_create(side);
    lv_obj_set_style_text_font(cap, theme::fontSmall(), 0);
    lv_obj_set_style_text_color(cap, theme::dim(), 0);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(cap, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cap, 160);
    lv_label_set_text_fmt(cap, "Or scan with a phone:\njoins \"%s\" and\nopens the Wi-Fi page", setupSsid);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 166);
    widgets::show(scr);
}

bool wifiHandle(const input::Event& e) {
    if (e.kind == input::Kind::Scroll) { widgets::listMove(lst, e.value); return true; }
    return false;
}

// Selected SSID, or "" when "Type a network name" is highlighted.
String wifiSelected() {
    return lst.selected < (int)ssids.size() ? ssids[lst.selected] : String("");
}

// ---------- Password strip ----------

static lv_obj_t* pwScr = nullptr;
static lv_obj_t* field = nullptr;
static lv_obj_t* modeLbl = nullptr;
static lv_obj_t* tiles[11];
static String typed;
static int mode = 0;   // 0 abc, 1 ABC, 2 123, 3 symbols
static int cursor = 0;
static uint32_t revealUntil = 0;
static const char* kSets[] = {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                              "0123456789", "!@#$%^&*()-_=+[]{};:,.?/ "};
static const char* kModeNames[] = {"abc", "ABC", "123", "!@#"};

static int setLen() { return strlen(kSets[mode]) + 2; }  // + DEL + DONE

static void drawField() {
    String shown;
    for (size_t i = 0; i < typed.length(); i++) {
        bool last = i + 1 == typed.length();
        shown += (last && millis() < revealUntil) ? String(typed[i]) : String("*");
    }
    lv_label_set_text(field, shown.c_str());
}

static void drawStrip() {
    int n = setLen();
    for (int t = 0; t < 11; t++) {
        int idx = cursor - 5 + t;
        lv_obj_t* tile = tiles[t];
        lv_obj_t* lbl = lv_obj_get_child(tile, 0);
        if (idx < 0 || idx >= n) { lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_HIDDEN);
        bool cur = idx == cursor;
        bool act = idx >= n - 2;
        const char* text = act ? (idx == n - 2 ? "DEL" : "DONE") : nullptr;
        char one[2] = {kSets[mode][idx], 0};
        lv_label_set_text(lbl, text ? text : one);
        lv_obj_set_style_text_font(lbl, act ? theme::fontSmall() : (cur ? theme::fontRow() : theme::fontTitle()), 0);
        lv_obj_set_size(tile, act ? 50 : (cur ? 54 : 28), cur ? 70 : 40);
        lv_obj_set_style_bg_color(tile, cur ? theme::hilite() : (act ? theme::green() : theme::sep()), 0);
        lv_obj_set_style_text_color(lbl, (cur || act) ? theme::white() : theme::text(), 0);
        lv_obj_center(lbl);
    }
    lv_label_set_text_fmt(modeLbl, "%s   (PAUSE switches)", kModeNames[mode]);
}

void passwordShow(const char* ssid) {
    typed = "";
    mode = 0;
    cursor = 0;
    pwScr = widgets::screen();
    widgets::Header h = widgets::header(pwScr, ssid, true);
    widgets::headerText(h, "password");

    lv_obj_t* box = lv_obj_create(pwScr);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, 14, 48);
    lv_obj_set_size(box, 452, 52);
    lv_obj_set_style_bg_color(box, theme::white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, theme::header(), 0);
    lv_obj_set_style_border_width(box, 3, 0);
    field = lv_label_create(box);
    lv_obj_set_style_text_font(field, theme::fontRow(), 0);
    lv_obj_set_style_text_color(field, theme::text(), 0);
    lv_obj_align(field, LV_ALIGN_LEFT_MID, 10, 0);

    modeLbl = lv_label_create(pwScr);
    lv_obj_set_style_text_font(modeLbl, theme::fontSmall(), 0);
    lv_obj_set_style_text_color(modeLbl, theme::dim(), 0);
    lv_obj_set_pos(modeLbl, 14, 108);

    lv_obj_t* strip = lv_obj_create(pwScr);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, 14, 228);
    lv_obj_set_size(strip, 452, 74);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(strip, 3, 0);
    for (int t = 0; t < 11; t++) {
        tiles[t] = lv_obj_create(strip);
        lv_obj_remove_style_all(tiles[t]);
        lv_obj_set_style_radius(tiles[t], 4, 0);
        lv_obj_set_style_bg_opa(tiles[t], LV_OPA_COVER, 0);
        lv_obj_t* l = lv_label_create(tiles[t]);
        lv_obj_set_style_text_font(l, theme::fontTitle(), 0);
    }
    drawField();
    drawStrip();
    widgets::show(pwScr);
}

// Returns: 0 handled, 1 DONE pressed (password in passwordText), -1 not handled.
int passwordHandle(const input::Event& e) {
    using input::Kind;
    int n = setLen();
    if (e.kind == Kind::Scroll) { cursor = ((cursor + e.value) % n + n) % n; drawStrip(); return 0; }
    if (e.kind == Kind::Pause) { mode = (mode + 1) % 4; cursor = 0; drawStrip(); return 0; }
    if (e.kind == Kind::Ok) {
        if (cursor == n - 2) { if (typed.length()) typed.remove(typed.length() - 1); drawField(); return 0; }
        if (cursor == n - 1) return 1;
        if (typed.length() < 63) { typed += kSets[mode][cursor]; revealUntil = millis() + 2000; drawField(); }
        return 0;
    }
    return -1;
}

void passwordTick() {
    if (pwScr && lv_screen_active() == pwScr && revealUntil && millis() > revealUntil) { revealUntil = 0; drawField(); }
}

const String& passwordText() { return typed; }

}  // namespace ui
