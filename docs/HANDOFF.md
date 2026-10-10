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
- Repo made public and `v0.1.0` cut on 2026-10-10 (release run 38056187020). Verified
  from the Mac exactly as the device fetches: `releases/latest/download/manifest.json`
  returns version 0.1.0, size 1,311,456, sha256 c58e6292…4482; `firmware.bin` is one
  302 to `release-assets.githubusercontent.com` then 200; downloaded sha256 matches
  the manifest; first byte 0xE9 (ESP32 image magic); `channels` release serves 20 rows.
  The signed redirect URL is about 800 characters, which esp_http_client handles by
  allocating the Location header dynamically, but that is on-device and unverified.

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
- esp_http_client following GitHub's 302 to `release-assets.githubusercontent.com`
  (about 800 characters of signed URL) for the `firmware.bin` download.
- (Resolved) The 20 feed URLs: the first `channels` run failed two, Marketplace and WTF;
  both fixed and all 20 validated on 2026-10-10.
- KCX_BT_EMITTER AT command behaviour (scan list, auto-reconnect).

## Next steps, in order

1. Bench: `scripts/flash.sh` on one Sunton board, join `Jukebox-Setup` from a phone,
   watch it self-update from 0.0.0-dev to 0.1.0 and print 20 channels. First hardware
   proof of TLS, redirect, OTA write and LittleFS.
2. Streaming + screen spike on the Sunton.
3. Resolver service (Mac Studio): feeds → episode lists, resume positions.
4. LVGL screens 1–3, then settings, Wi-Fi, Bluetooth.
5. Enclosure on the H2C.
