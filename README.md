# Podcast Jukebox

A one-purpose podcast player for someone who does not want an app: a 3.5-inch screen,
two knobs, three buttons, a speaker, Bluetooth out to headphones. Twenty channels, each
a public RSS feed. Built on the parts bin shared with AnandRadio.

Status: **design + update plumbing**. The firmware in this repo joins Wi-Fi, updates
itself from GitHub Releases, loads the channel list, and stops there. Screen, knobs,
audio and the podcast resolver come next. See `docs/HANDOFF.md`.

## How it works

| Piece | Where | Notes |
|---|---|---|
| Screens | `docs/screens/podcast-jukebox-screens.html` | All 7 screens at native 480 × 320. Open in a browser. |
| Channel list | `channels.md` | Edit the table, push to `main`. Devices pick it up on next boot. |
| Firmware | `src/` | PlatformIO, arduino-esp32 3.x via the pioarduino platform. |
| Releases | GitHub Releases | `scripts/release.sh 0.2.0` tags; CI builds and publishes `firmware.bin` + `manifest.json`. |
| Updates | `src/ota.cpp` | On every boot: fetch manifest, compare version, stream the image into the spare OTA slot, verify sha256, reboot. |

## Hardware

- Sunton ESP32-3248S035R (ESP32-WROOM-32, 4 MB flash, no PSRAM, 480 × 320 ST7796, touch disabled)
- MAX98357A I2S amp + 50 mm speaker in a sealed chamber
- PCM5102A I2S DAC → KCX_BT_EMITTER Bluetooth transmitter (UART-controlled) → his headphones
- EC11 encoder with push (scroll / OK), B10K pot (volume), 3 × 30 mm arcade buttons (OK, PAUSE, STOP)
- USB-C 5 V, mains powered

## Build and flash

```bash
pio run                                  # build (FW_VERSION defaults to 0.0.0-dev)
pio run -t upload --upload-port /dev/cu.usbserial-XXXX
pio device monitor
```

First boot with no saved Wi-Fi opens a hotspot named `Jukebox-Setup` for three minutes.
Join it from a phone and pick the network. Credentials go to NVS, never to this repo.

## Ship an update

```bash
scripts/release.sh 0.2.0     # tags v0.2.0 and pushes; watch with: gh run watch
```

Every device checks `releases/latest/download/manifest.json` at boot and updates if the
version is newer than the one it runs. A dev build (`0.0.0-dev`) always updates.

## Change the channels

Edit `channels.md`, commit, push to `main`. The `channels` workflow fetches every feed
and refuses to publish if one does not answer with XML. Published copy lives on the
rolling `channels` release, which is what devices download.

## Repo visibility

The repo is private. GitHub serves release assets of a private repo only with a token,
so the device cannot fetch updates until one of these is done:

1. make the repo public (`gh repo edit --visibility public`), nothing secret is in it, or
2. mirror `manifest.json`, `firmware.bin` and `channels.md` to a public HTTPS host and
   point the device there (NVS keys `ota/manifest` and `ota/channels`).

See `docs/decisions/003-ota-via-github-releases.md`.
