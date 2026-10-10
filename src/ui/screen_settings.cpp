// Screens 04 and 07: Settings and Bluetooth. Both are plain lists on the shared
// virtual-list widget, with a value column and status dots.

#include <WiFi.h>

#include "../bt_emitter.h"
#include "../settings.h"
#include "../version.h"
#include "theme.h"
#include "ui.h"
#include "widgets.h"

namespace ui {

// ---------- Settings ----------

static lv_obj_t* scr = nullptr;
static widgets::Header hdr;
static widgets::List lst;
static String wifiVal, btVal, verVal;

static const char* kRows[] = {"Wi-Fi", "Bluetooth", "Sound output", "Sleep timer",
                              "Check for new episodes", "About", "Back to channels"};
static const int kRowCount = 7;

static void provideSettings(int i, widgets::RowData& d) {
    d.name = kRows[i];
    switch (i) {
        case 0:
            d.value = wifiVal.c_str();
            d.dot = WiFi.status() == WL_CONNECTED ? 0x2F7D4F : 0xB7791F;
            break;
        case 1:
            d.value = btVal.c_str();
            d.dot = !bt::fitted() ? 0xC9C3B6 : (bt::status().startsWith("Connected") ? 0x2F7D4F : 0xB7791F);
            break;
        case 2: d.value = settings::output() == settings::Output::Bluetooth ? "Bluetooth" : "Speaker"; break;
        case 3: d.value = settings::sleepLabel(); break;
        case 4: d.value = "Now"; break;
        case 5: d.value = verVal.c_str(); break;
        case 6: d.separator = true; break;
    }
}

void settingsRefresh() {
    wifiVal = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("Not connected");
    btVal = bt::status();
    verVal = String("v") + fwVersion();
    if (scr && lv_screen_active() == scr) widgets::listRedraw(lst);
}

void settingsShow(int selected) {
    settingsRefresh();
    scr = widgets::screen();
    hdr = widgets::header(scr, "SETTINGS", true);
    widgets::list(lst, scr, 8, nullptr);
    lst.provider = provideSettings;
    lst.selected = selected;
    widgets::listSetTotal(lst, kRowCount);
    widgets::show(scr);
}

bool settingsHandle(const input::Event& e) {
    if (e.kind == input::Kind::Scroll) { widgets::listMove(lst, e.value); return true; }
    return false;
}

int settingsSelected() { return lst.selected; }

// ---------- Bluetooth ----------

static lv_obj_t* btScr = nullptr;
static widgets::Header btHdr;
static widgets::List btLst;
static String btLine[8];

static void provideBt(int i, widgets::RowData& d) {
    const auto& devs = bt::devices();
    if (!bt::fitted()) {
        if (i == 0) { d.name = "Bluetooth module not fitted"; d.separator = true; }
        else { d.name = "Back to settings"; d.separator = true; }
        return;
    }
    if (i == 0) { d.name = "Put your headphones in pairing mode"; d.separator = true; return; }
    int di = i - 1;
    if (di < (int)devs.size()) {
        const bt::Device& dev = devs[di];
        d.name = dev.name.c_str();
        d.value = dev.connected ? "Connected" : (dev.paired ? "Paired" : "New");
        d.dot = dev.connected ? 0x2F7D4F : (dev.paired ? 0xB7791F : 0xC9C3B6);
        return;
    }
    int tail = di - (int)devs.size();
    if (tail == 0) { d.name = "Sound output"; d.value = settings::output() == settings::Output::Bluetooth ? "Bluetooth" : "Speaker"; }
    else if (tail == 1) { d.name = "Forget this device"; }
    else { d.name = "Back to settings"; d.separator = true; }
}

static int btRowCount() { return bt::fitted() ? (int)bt::devices().size() + 4 : 2; }

void bluetoothShow() {
    btScr = widgets::screen();
    btHdr = widgets::header(btScr, "BLUETOOTH", true);
    widgets::list(btLst, btScr, 8, nullptr);
    btLst.provider = provideBt;
    btLst.selected = bt::fitted() ? 1 : 1;
    widgets::listSetTotal(btLst, btRowCount());
    widgets::headerText(btHdr, bt::fitted() ? "Searching..." : "");
    if (bt::fitted()) bt::startScan();
    widgets::show(btScr);
}

void bluetoothRefresh() {
    if (!btScr || lv_screen_active() != btScr) return;
    widgets::listSetTotal(btLst, btRowCount());
    widgets::headerText(btHdr, bt::scanning() ? "Searching..." : "");
}

bool bluetoothHandle(const input::Event& e) {
    if (e.kind == input::Kind::Scroll) { widgets::listMove(btLst, e.value); return true; }
    return false;
}

// Returns which row was confirmed: device index >= 0, or -1 output, -2 forget, -3 back, -4 nothing
int bluetoothAction() {
    int i = btLst.selected;
    if (!bt::fitted()) return i == 1 ? -3 : -4;
    int di = i - 1;
    int n = (int)bt::devices().size();
    if (di >= 0 && di < n) return di;
    int tail = di - n;
    if (tail == 0) return -1;
    if (tail == 1) return -2;
    if (tail == 2) return -3;
    return -4;
}

}  // namespace ui
