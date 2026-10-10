#pragma once
#include <Arduino.h>

#include <vector>

// KCX_BT_EMITTER Bluetooth audio transmitter on Serial2 (AT command set). Until the
// module is fitted every call reports "not fitted" and the UI shows that plainly.
// Decision 002: audio leaves the ESP32 over I2S -> PCM5102A line-out -> this module.
namespace bt {

struct Device {
    String name;
    String addr;      // "aa:bb:cc:dd:ee:ff"
    bool connected = false;
    bool paired = false;
};

static const int kPinRx = 35;   // input-only pin reads the module's TX
static const int kPinTx = 17;   // blue LED pin drives the module's RX

void begin();
bool fitted();                       // module answered AT
bool scanning();
void startScan();                    // AT+SCAN; results accumulate for ~10 s
const std::vector<Device>& devices();
bool connect(const Device& d);       // AT+CONADD
bool disconnect();
bool forget(const Device& d);        // AT+DELVMLINK (clears stored link)
String status();                     // "Connected to X" / "Not connected" / "Not fitted"
void loop();                         // parse unsolicited lines

}  // namespace bt
