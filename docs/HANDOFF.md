# Handoff

Last updated 2026-10-10. The memory chunk "Podcast Jukebox design 2026-10-10" in Oracle
points here; this file holds the operational detail.

## State

- Design frozen enough to build: 7 screens mocked at native 480 × 320 in
  `docs/screens/podcast-jukebox-screens.html`. Control grammar in decision 001.
- Firmware skeleton builds. It joins Wi-Fi (WiFiManager portal `Jukebox-Setup` on first
  run), runs the boot-time update check (`src/ota.cpp`), loads `channels.md`
  (`src/channels.cpp`, cached in LittleFS), prints the list, idles.
- CI: `build.yml` on every push, `release.yml` on `v*` tags, `channels.yml` when
  `channels.md` changes on `main`.
- Nothing has run on hardware yet.

## Evidence

- `pio run` compiles the skeleton locally on 2026-10-10: RAM 50,240 B of 327,680 (15%),
  flash 1,289,716 B of 1,966,080 (66%) with PlatformIO Core 6.2.0, pioarduino
  platform 55.3.312 (arduino-esp32 3.3.x). Two thirds of the OTA slot is gone before
  LVGL or an audio decoder is linked; expect to trim the IDF cert bundle to a few roots
  and drop WiFiManager's HTML once the real UI exists.
- Mockup verified in a browser via Playwright at 1700 px and at the 1:1 panel size.
- The update path and the channel fetch are **not** verified against a real release,
  because the repo is private and releases of a private repo need a token (decision 003).

## Constraints (do not re-litigate)

- Controls: SCROLL (push = OK), VOLUME, OK / PAUSE / STOP. Touch disabled. Decision 001.
- Bluetooth is output to headphones through a KCX_BT_EMITTER, not ESP32 A2DP. Decision 002.
- Firmware and channel list come from GitHub Releases, no secrets in firmware. Decision 003.
- `channels.md` format: one table, `| n | name | https://feed |`, at most 20 rows, names
  under 24 characters. The device parser and the CI validator both depend on it.
- Partition table `min_spiffs.csv`: two 1.9 MB app slots. If the image outgrows that,
  shrink the image (cert bundle, debug level, WiFiManager) or move to a 16 MB board.
  Never go to a single-slot table: that is the end of remote updates.

## Nobody has verified

- That the Sunton's ESP32 (no PSRAM) can stream MP3 over HTTPS while LVGL drives a
  480 × 320 panel. This is the go / no-go spike before any enclosure work.
- `esp_crt_bundle_attach` under the pioarduino arduino-esp32 3.x build; it compiles, but
  a live TLS fetch to GitHub has not been run.
- esp_http_client following GitHub's 302 to `objects.githubusercontent.com` for the
  `firmware.bin` download.
- The 20 feed URLs in `channels.md`. The `channels` workflow checks them on first push;
  expect one or two to need fixing.
- KCX_BT_EMITTER AT command behaviour (scan list, auto-reconnect).

## Next steps, in order

1. Decide repo visibility or a mirror host (decision 003), then push a `v0.1.0` tag and
   watch one device update itself.
2. Streaming + screen spike on the Sunton.
3. Resolver service (Mac Studio): feeds → episode lists, resume positions.
4. LVGL screens 1–3, then settings, Wi-Fi, Bluetooth.
5. Enclosure on the H2C.
