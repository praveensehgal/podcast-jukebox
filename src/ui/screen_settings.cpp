// 04 Settings, 05 Wi-Fi, 07 Bluetooth, 08 Set up with a phone: all paged simple-row lists
// except the phone screen.

#include "../bt_emitter.h"
#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

static widgets::Paged lst;
static widgets::SimpleRow rows[4];

static void makeRows(lv_obj_t* r, int i) { widgets::makeSimpleRow(rows[i], r); }

static lv_obj_t* listScreen(const char* title, const char* status, std::function<void(int, int)> bind, int total, int page) {
    lv_obj_t* scr = widgets::screen();
    widgets::HeaderOpts o;
    o.back = true;
    o.status = status;
    widgets::header(scr, title, o);
    lst = widgets::Paged();
    widgets::paged(lst, scr, 4, theme::kRowH, makeRows);
    lst.bind = bind;
    widgets::pagedSet(lst, total, page);
    return scr;
}

// ---------- 04 Settings ----------

static SettingsView sv;
static String wifiVal, btVal;

static void bindSettings(int slot, int index) {
    widgets::RowData d;
    switch (index) {
        case kRowWifi: d.name = "Wi-Fi"; d.value = wifiVal.c_str(); d.dot = sv.wifiOk ? 0x2F7D4F : 0xB7791F; break;
        case kRowBluetooth: d.name = "Bluetooth"; d.value = btVal.c_str(); d.dot = sv.btDot; break;
        case kRowSound: d.name = "Sound from"; d.value = sv.soundBluetooth ? "Bluetooth" : "Speaker"; d.pill = true; d.pillDark = true; d.chev = ""; break;
        case kRowSleep: d.name = "Sleep timer"; d.value = sv.sleep; d.pill = true; d.chev = ""; break;
        case kRowRefresh: d.name = "Check for new episodes"; d.chev = LV_SYMBOL_REFRESH; break;
        case kRowAbout: d.name = "About"; d.value = sv.version; d.chev = ""; break;
    }
    widgets::bindSimpleRow(rows[slot], d);
}

void settingsShow(const SettingsView& v, int page) {
    sv = v;
    wifiVal = v.wifi;
    btVal = v.bluetooth;
    widgets::show(listScreen("Settings", nullptr, bindSettings, kSettingsRows, page));
}

// ---------- 05 Wi-Fi ----------

static std::vector<Net> nets;

static void bindWifi(int slot, int index) {
    widgets::RowData d;
    if (index == 0) {
        d.name = "Set up with a phone";
        d.tint = 0xEEF3FB;
    } else {
        const Net& n = nets[index - 1];
        d.name = n.ssid.c_str();
        d.bars = n.rssi > -55 ? 4 : n.rssi > -65 ? 3 : n.rssi > -75 ? 2 : 1;
        d.value = n.open ? "open" : nullptr;
    }
    widgets::bindSimpleRow(rows[slot], d);
}

void wifiShow(const std::vector<Net>& list, bool scanning) {
    nets = list;
    widgets::show(listScreen("Wi-Fi", scanning ? "Scanning..." : nullptr, bindWifi, (int)nets.size() + 1, 0));
}

// ---------- 07 Bluetooth ----------

static bool btFitted = false;

static void bindBt(int slot, int index) {
    widgets::RowData d;
    const auto& devs = bt::devices();
    if (index == 0) {
        d.name = btFitted ? "Put the headphones in pairing mode" : "Bluetooth module not fitted yet";
        d.muted = true;
        d.tint = 0xECE7DC;
    } else {
        const bt::Device& dev = devs[index - 1];
        d.name = dev.name.c_str();
        d.value = dev.connected ? "Connected" : (dev.paired ? "Paired" : "New");
        d.dot = dev.connected ? 0x2F7D4F : (dev.paired ? 0xB7791F : 0xC9C3B4);
        d.chev = "";
    }
    widgets::bindSimpleRow(rows[slot], d);
}

void bluetoothShow(bool fitted, bool scanning) {
    btFitted = fitted;
    int total = fitted ? (int)bt::devices().size() + 1 : 1;
    widgets::show(listScreen("Bluetooth", scanning ? "Searching..." : nullptr, bindBt, total, 0));
}

void bluetoothRefresh(bool) {
    if (!lst.bind) return;
    int total = btFitted ? (int)bt::devices().size() + 1 : 1;
    if (total != lst.total) widgets::pagedSet(lst, total, lst.page);
}

// ---------- 08 Set up with a phone ----------

void phoneShow(const char* ssid, const char* key) {
    lv_obj_t* scr = widgets::screen();
    widgets::HeaderOpts o;
    o.back = true;
    widgets::header(scr, "Set up with a phone", o);
    String payload = String("WIFI:T:WPA;S:") + ssid + ";P:" + key + ";;";
    lv_obj_t* qr = lv_qrcode_create(scr);
    lv_qrcode_set_size(qr, 168);
    lv_qrcode_set_dark_color(qr, theme::text());
    lv_qrcode_set_light_color(qr, theme::white());
    lv_qrcode_update(qr, payload.c_str(), payload.length());
    lv_obj_set_pos(qr, 22, 66);
    lv_obj_t* t = widgets::text(scr, theme::fontTitle(), theme::text(),
                                "1. Scan this with the\nphone camera.\n2. Join Jukebox-Setup.\n3. Pick the home network\nand type its password.");
    lv_obj_set_pos(t, 212, 70);
    lv_obj_t* k = widgets::text(scr, theme::fontSmall(), theme::dim(), (String("Key ") + key + "   -   192.168.4.1").c_str());
    lv_obj_set_pos(k, 22, 290);
    widgets::show(scr);
    lv_refr_now(nullptr);
}

}  // namespace ui
