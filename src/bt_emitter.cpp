// KCX_BT_EMITTER driver. The AT dialect (V1.7 firmware) as documented by the vendor
// and the community:
//   AT+          -> OK+            probe
//   AT+SCAN      -> New Devices: ... / MacAdd:... Name:...   lines for ~10 s
//   AT+CONADD=<mac>  -> CONNECTED / "Connected MacAdd:..."
//   AT+DISCON    -> DISCONNECT
//   AT+DELVMLINK -> Delete_Vmlink
//   AT+STATUS    -> BT_ADD / CONNECTED...
// None of this has run against real hardware yet; the parser is deliberately loose
// and every result is logged so the first bench session can tune it.

#include "bt_emitter.h"

namespace bt {

static bool present = false;
static bool scanActive = false;
static uint32_t scanEndsAt = 0;
static std::vector<Device> found;
static String connectedName;
static String lineBuf;

static void send(const char* cmd) {
    Serial2.print(cmd);
    Serial2.print("\r\n");
    Serial.printf("[bt] > %s\n", cmd);
}

void begin() {
    Serial2.begin(9600, SERIAL_8N1, kPinRx, kPinTx);
    send("AT+");
    uint32_t until = millis() + 400;
    String reply;
    while (millis() < until) {
        while (Serial2.available()) reply += (char)Serial2.read();
    }
    present = reply.indexOf("OK") >= 0;
    Serial.printf("[bt] module %s\n", present ? "fitted" : "not fitted");
}

bool fitted() { return present; }
bool scanning() { return scanActive && millis() < scanEndsAt; }

void startScan() {
    if (!present) return;
    found.clear();
    scanActive = true;
    scanEndsAt = millis() + 10000;
    send("AT+SCAN");
}

const std::vector<Device>& devices() { return found; }

bool connect(const Device& d) {
    if (!present) return false;
    String cmd = "AT+CONADD=" + d.addr;
    cmd.replace(":", "");
    send(cmd.c_str());
    return true;
}

bool disconnect() {
    if (!present) return false;
    send("AT+DISCON");
    connectedName = "";
    return true;
}

bool forget(const Device&) {
    if (!present) return false;
    send("AT+DELVMLINK");
    connectedName = "";
    return true;
}

String status() {
    if (!present) return "Not fitted";
    if (connectedName.length()) return "Connected: " + connectedName;
    return "Not connected";
}

static void parseLine(const String& l) {
    Serial.printf("[bt] < %s\n", l.c_str());
    int m = l.indexOf("MacAdd:");
    int n = l.indexOf("Name:");
    if (m >= 0 && n > m) {
        Device d;
        d.addr = l.substring(m + 7, n);
        d.addr.trim();
        d.name = l.substring(n + 5);
        d.name.trim();
        for (auto& e : found) if (e.addr == d.addr) return;
        found.push_back(d);
        return;
    }
    if (l.indexOf("CONNECTED") >= 0 || l.indexOf("Connected") >= 0) {
        for (auto& e : found) if (l.indexOf(e.addr) >= 0) { e.connected = true; connectedName = e.name; }
        if (!connectedName.length()) connectedName = "headphones";
    }
    if (l.indexOf("DISCONNECT") >= 0) {
        for (auto& e : found) e.connected = false;
        connectedName = "";
    }
}

void loop() {
    if (!present) return;
    while (Serial2.available()) {
        char c = (char)Serial2.read();
        if (c == '\n' || c == '\r') {
            if (lineBuf.length()) parseLine(lineBuf);
            lineBuf = "";
        } else if (lineBuf.length() < 120) {
            lineBuf += c;
        }
    }
    if (scanActive && millis() >= scanEndsAt) scanActive = false;
}

}  // namespace bt
