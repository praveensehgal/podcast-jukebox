#include "settings.h"

#include <Preferences.h>

namespace settings {

static Preferences prefs;
static bool opened = false;
static void ensure() { if (!opened) { prefs.begin("set", false); opened = true; } }

Output output() { ensure(); return (Output)prefs.getUChar("out", 0); }
void setOutput(Output o) { ensure(); prefs.putUChar("out", (uint8_t)o); }

uint8_t volume() { ensure(); return prefs.getUChar("vol", 11); }
void setVolume(uint8_t v) { ensure(); prefs.putUChar("vol", v > 16 ? 16 : v); }

uint16_t sleepMinutes() { ensure(); return prefs.getUShort("sleep", 0); }
void setSleepMinutes(uint16_t m) { ensure(); prefs.putUShort("sleep", m); }

const char* sleepLabel() {
    switch (sleepMinutes()) {
        case 30: return "30 min";
        case 60: return "60 min";
        case 90: return "90 min";
        default: return "Off";
    }
}

}  // namespace settings
