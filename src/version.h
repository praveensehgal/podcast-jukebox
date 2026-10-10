#pragma once

// FW_VERSION is injected by the build (platformio.ini reads $FW_VERSION; CI sets it
// from the git tag, e.g. v0.3.1 -> "0.3.1"). A local build with the variable unset
// gets an empty string, which fwVersion() turns into "0.0.0-dev". The updater treats
// that as older than any release, so a dev board self-updates on its next boot.
#ifndef FW_VERSION
#define FW_VERSION ""
#endif

#define FW_NAME "podcast-jukebox"

inline const char* fwVersion() {
    return FW_VERSION[0] ? FW_VERSION : "0.0.0-dev";
}
