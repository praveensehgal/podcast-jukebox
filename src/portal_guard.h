#pragma once
#include <WiFiManager.h>

// WiFiManager registers its firmware-upload page (/update, /u) and its erase, restart,
// info and parameter pages on every portal, whatever the menu shows, and it has no
// switch to leave them out. It does call a user hook before it registers them, and the
// web server serves the first handler registered for a path, so answering those paths
// here takes them away. The setup portal is then Wi-Fi setup and nothing else: the only
// way to write firmware is the signed-hash updater in ota.cpp.
namespace portal_guard {

inline void lockDown(WiFiManager& wm) {
    wm.setWebServerCallback([&wm]() {
        static const char* const kBlocked[] = {"/update", "/u", "/erase", "/restart", "/info", "/param", "/paramsave"};
        for (const char* path : kBlocked) {
            wm.server->on(path, HTTP_ANY, [&wm]() { wm.server->send(404, "text/plain", "Not found"); });
        }
    });
}

}  // namespace portal_guard
