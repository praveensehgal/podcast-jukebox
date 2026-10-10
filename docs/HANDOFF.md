# Handoff

Last updated 2026-10-10. The memory chunk "Podcast Jukebox design 2026-10-10" in Oracle
points here; this file holds the operational detail.

## State (2026-10-10 evening, after the screen build)

All seven screens from the mockup are implemented and run on the Sunton. The firmware
streams real episodes. What is missing is hardware, not software: the encoder, pot
and buttons are not wired (serial keys stand in), the MAX98357A and speaker are not
wired (decode proven, sound not yet heard), and the KCX_BT_EMITTER arrives 2026-10-11.

Build stages and what proved them:

| Stage | What | Proof on the bench |
|---|---|---|
| A | LovyanGFX + LVGL 9, boot status screen, channel list, serial stand-in input | Screen renders; "open channel 6 Fresh Air" from the serial keys |
| spike | ESP8266Audio Helix MP3 over HTTPS, persistent I2S | 52 s of NPR News Now decoded with the list on screen, 53 KB heap free |
| C/D | resolver client, episode list, Now Playing, state machine | 3 consecutive channel → play → back → pause → resume → stop cycles; resume at 2:02 from a saved position |
| E | Settings, Wi-Fi scan + QR, password strip, Bluetooth, sleep timer | Full tour twice, 12 networks found, no crash |

Pin map as wired in firmware (Sunton free pins are scarce; see `input.cpp`, `audio_out.h`, `bt_emitter.h`):

| Function | GPIO | Note |
|---|---|---|
| I2S BCLK / LRCLK / DOUT | 22 / 21 / 16 | 16 is the green LED pin; it flickers with data |
| KCX_BT_EMITTER RX / TX | 35 (module TX) / 17 (module RX) | 35 is input-only, so the UART driver logs a harmless pull-up error |
| Encoder A / B / push | 18 / 19 / 23 | planned, SD-card pins, internal pull-ups |
| OK / PAUSE / STOP buttons | 5 / 33 / 0 | planned; 0 is the boot button |
| Volume pot | 34 | planned, ADC, input-only |
| Backlight | 27 | PWM via LovyanGFX |
| Touch CS | 33 | held high so the XPT2046 stays off the SPI bus |

Serial stand-in keys on a dev build: j/k scroll, o OK, p PAUSE, s STOP, S hold-STOP,
+/- volume, t bench episode.

Resolver: runs on the Mac Studio for the bench (`uv run uvicorn jukebox_resolver.server:app --host 0.0.0.0 --port 8787` from `resolver/`), device default `http://192.168.50.116:8787`, overridable in NVS `ota/resolver`. Moves to EC2 behind HTTPS + token before the device leaves the house.

## State (earlier the same day)

- Design frozen enough to build: 7 screens mocked at native 480 × 320 in
  `docs/screens/podcast-jukebox-screens.html`. Control grammar in decision 001.
- Firmware skeleton builds. It joins Wi-Fi (WiFiManager portal `Jukebox-Setup` on first
  run), runs the boot-time update check (`src/ota.cpp`), loads `channels.md`
  (`src/channels.cpp`, cached in LittleFS), prints the list, idles.
- CI: `build.yml` on every push, `release.yml` on `v*` tags, `channels.yml` when
  `channels.md` changes on `main`.
- Runs on hardware. Sunton MAC d4:8c:49:57:1a:24 on the bench (USB port shows as
  `cu.wchusbserial2140` on the Mac Studio). On 2026-10-10 a 0.0.0-dev build found
  v0.1.1 on GitHub, streamed 1,314,816 bytes through two redirects, verified the
  sha256, flipped to app1, rebooted into the CI-built 0.1.1, reported "up to date" and
  listed 20 channels. The v0.1.0 image cannot follow redirects (see decision 003
  notes) and is superseded.

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
- (Resolved 2026-10-10) TLS via `esp_crt_bundle_attach`, the two-hop redirect to
  `release-assets.githubusercontent.com`, the OTA write, LittleFS on min_spiffs: all
  proven on the bench. Two bugs were found and fixed on the way: streaming-mode
  esp_http_client does not auto-follow redirects, and the default 512-byte TX buffer
  cannot hold the ~800-character signed URL. Both live in `src/http_get.cpp`.
- (Resolved 2026-10-10) WiFiManager WPA2 portal: after an NVS erase the board opened
  `Jukebox-Setup` with its random key, Praveen joined from his phone, picked the
  network, and the board connected, checked for updates and loaded channels. The
  whole first-boot path is proven.
- (Resolved) The 20 feed URLs: the first `channels` run failed two, Marketplace and WTF;
  both fixed and all 20 validated on 2026-10-10.
- KCX_BT_EMITTER AT command behaviour (scan list, auto-reconnect).

## Next steps, in order

1. (Done 2026-10-10) Bench flash and self-update from 0.0.0-dev to 0.1.1.
2. Streaming + screen spike on the Sunton.
3. Resolver service (Mac Studio): feeds → episode lists, resume positions.
4. LVGL screens 1–3, then settings, Wi-Fi, Bluetooth.
5. Enclosure on the H2C.
