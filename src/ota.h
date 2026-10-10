#pragma once
#include <Arduino.h>

namespace ota {

// Result of one update check. Logged, and later shown on the About screen.
enum class Result {
    UpToDate,        // manifest version <= running version
    Updated,         // new image flashed; device is about to reboot
    NoNetwork,       // Wi-Fi not connected, check skipped
    ManifestError,   // could not fetch or parse manifest.json
    DownloadError,   // image fetch or flash failed; old image still active
    HashMismatch,    // image downloaded but sha256 != manifest; discarded
};

struct Manifest {
    String version;   // "0.3.1"
    String url;       // https://.../firmware.bin
    String sha256;    // 64 hex chars
    uint32_t size;    // bytes, for progress and sanity
};

// Where the device looks. Default is this repo's GitHub Release "latest".
// Overridable from NVS (namespace "ota", key "manifest") so a mirror can be used
// without reflashing. See docs/ota.md.
String manifestUrl();
void setManifestUrl(const String& url);

// Fetch manifest, compare to FW_VERSION, flash if newer, reboot on success.
// Blocking; call once at boot after Wi-Fi is up. Progress goes to Serial.
Result checkAndUpdate();

// Semantic version compare: returns <0, 0, >0 like strcmp. "0.0.0-dev" < everything.
int compareVersions(const String& a, const String& b);

const char* resultName(Result r);

}  // namespace ota
