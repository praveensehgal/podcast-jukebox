# Handoff

Last updated 2026-10-10 (afternoon, touch build). The Oracle memory chunk "Podcast
Jukebox state" points here; this file holds the operational detail.

## State

The firmware on the bench Sunton (`/dev/cu.wchusbserial2140`, dev build `0.0.0-dev`) is
the touch build: eight screens driven by taps, three top buttons read on GPIO 18/19/23,
audio fetched through the resolver over plain HTTP. Not yet released: `v0.1.1` is the
last tag and it is the old knob build, which cannot play (see decision 005).

What is missing is hardware. The MAX98357A and speaker are not wired, so nobody has
heard sound. The three top switches are not wired (serial keys stand in). The
KCX_BT_EMITTER arrives 2026-10-11.

| Area | State | Proof on the bench, 2026-10-10 |
|---|---|---|
| Touch | working | Praveen tapped Back (14,12) and the page-down arrow (473,285); both landed on their targets with board 1's calibration |
| Channels, Episodes, Now Playing | working | serial tour: open channel, play, volume, skip +30/-30, pause, resume, Back, Now Playing pill, STOP, PLAY resumes at the saved second |
| Settings, Wi-Fi list, Bluetooth | working | tour: 13 to 15 networks listed, sound and sleep pills toggle, Bluetooth shows "module not fitted" |
| Password keyboard, phone QR setup | built, not exercised | compiles and opens; no join was attempted from the new keyboard or the new non-blocking portal |
| Playback | working through the relay | 16 KB/s read rate (128 kbps, real time), about 85 KB free with a stream open, 16 resumes and skips in one run with no reset, resume from the server's saved position after a reboot |
| Resolver | 138 tests, 96% coverage, ruff clean | `/stream` added with 19 tests |
| OTA, first-boot portal, channel list | working (earlier the same day) | 0.0.0-dev to 0.1.1 self-update; phone joined `Jukebox-Setup` |
| Enclosure D | one-filament 3MF regenerated | meshes watertight; not sliced on the H2C profile, not printed since the fix |

## Pin map

| Function | GPIO | Note |
|---|---|---|
| I2S BCLK / LRCLK / DOUT | 22 / 21 / 16 | to the MAX98357A; 16 is the green LED pin and flickers with data |
| PLAY / PAUSE / STOP switches | 18 / 19 / 23 | microSD socket lines; each switch to GND, internal pull-ups, 25 ms debounce; STOP held 3 s opens Settings |
| KCX_BT_EMITTER RX / TX | 35 (module TX) / 17 (module RX) | 35 is input-only, so the UART driver logs a harmless pull-up error at boot |
| Touch (XPT2046) | CS 33, IRQ 36, shares HSPI 14/13/12 with the panel | calibration constants in `src/display.cpp` are for board 1 |
| Backlight | 27 | PWM via LovyanGFX |

Serial stand-ins on a dev build: `P` play, `p` pause, `s` stop, `S` hold-stop, `b` back,
`g` gear, `n` now playing, `-` `+` volume, `<` `>` skip, `j` join, `1`-`8` pick list item
1-8, `t` bench episode (direct HTTPS; expected to fail for lack of memory, kept as the
probe for that). Every touch press logs `[touch] x,y`.

Driving it from a script: open the port with pyserial (`~/.platformio/penv/bin/python`),
set `dtr=False` and `rts=False` before `open()`, then pulse RTS to reset. `pio device
monitor` needs a TTY.

Resolver: runs on the Mac Studio for the bench (`uv run uvicorn jukebox_resolver.server:app
--host 0.0.0.0 --port 8787` from `resolver/`), device default `http://192.168.50.116:8787`,
overridable in NVS `ota/resolver`. It must be restarted after pulling this commit.

## Constraints (do not re-litigate)

- Every screen must be usable by touch alone. Now Playing carries its own Pause and Stop
  buttons, and no notice may end on "press a button": Praveen played an episode before
  the switches were wired and could not stop it (2026-10-10).
- Touch does navigation; PLAY / PAUSE / STOP are also physical. Lists page with arrows, never
  scroll. Decision 004, which replaces the knobs of 001.
- Audio comes through the resolver's `/stream` over plain HTTP. The device must not open
  a TLS session while playing: TLS (56-75 KB) plus the decoder (34 KB) plus the ring
  (16 KB) exceeds the ~105 KB it has. Decision 005. Do not "simplify" this back to
  direct CDN URLs.
- The LVGL draw buffer stays at 10 lines (9.4 KB). The 40-line buffer cost 28 KB of the
  heap playback needs.
- The ring buffer is taken with malloc after the stream opens. Not `new` (bad_alloc
  aborts), not a static array (it shrinks the largest heap region from 73 to 41 KB).
- Bluetooth is output to headphones through a KCX_BT_EMITTER, not ESP32 A2DP. Decision 002.
- Firmware and channel list come from GitHub Releases, no secrets in firmware. Decision 003.
- `channels.md` format: one table, `| n | name | https://feed |`, at most 20 rows, names
  under 24 characters. The device parser and the CI validator both depend on it.
- Partition table `min_spiffs.csv`: two 1.9 MB app slots; the image is at 88.8%. Never
  go to a single-slot table: that is the end of remote updates.
- The setup hotspot is WPA2 with a random per-device key in NVS (`net/setupkey2`).
  WiFiManager always registers its firmware-upload route, so the hotspot is never open.
- Keep `GuardedMP3` in `audio_out.cpp`. The library's own loop divides by the channel
  count and reset the board on a resume that landed mid-frame; it also hides a failed
  decoder allocation.
- Never pipe gitleaks through `tail` in a commit chain; that is how a real key got pushed.

## Nobody has verified

- Sound. No amp is wired. The decode is now paced by the I2S clock at the right byte
  rate, which the earlier "streaming works" result was not: that run had a decoder that
  silently failed to allocate, and nothing was being decoded.
- The portal lockdown (`src/portal_guard.h`): WiFiManager's /update, /u, /erase,
  /restart, /info and /param pages are answered with 404 by a handler registered ahead
  of the library's. Built and flashed, but nobody has joined the hotspot and requested
  those paths to see the 404.
- The on-screen Pause and Stop buttons under a finger (added after the tour; the same
  events pass from the serial keys). The touch mockup in docs/screens still shows the
  older Now Playing layout with four buttons and a state pill.
- The three physical switches, and whether GPIO 18/19/23 behave with real switches.
- Joining a network from the on-screen keyboard, and the non-blocking phone-setup portal
  started from the Wi-Fi screen (the boot-time portal was proven; this path is new).
- That a failed join really leaves the old network's credentials saved.
- Stream-drop recovery (`streamEnded` reopens up to three times) and the end of an
  episode played through to the last second.
- Touch calibration on any board other than board 1, and accuracy at the far left edge.
- KCX_BT_EMITTER: the driver assumes `AT+SCAN` / `AT+CONADD`. V1.7 firmware uses
  115200 baud and `AT+ADDLINKADD` + `AT+PAIR` instead, and expects 5 V serial levels.
  The driver must read `AT+GMR?` and branch. The PCM5102A's 2.1 Vrms line-out likely
  needs a divider (about 10 k / 4.7 k per channel) into the module.
- The resolver anywhere but the Mac: no token is sent by the firmware yet.

## Next steps, in order

1. Wire the MAX98357A and speaker (docs/amp-wiring-guide.html) and listen.
2. Wire the three switches to 18/19/23 and print the caps.
3. Exercise the keyboard join and the phone portal from the Wi-Fi screen.
4. KCX module on arrival: version probe, then the driver branch.
5. Resolver to EC2 with a token, then cut `v0.2.0` (the first release that can play).
6. Print the fixed enclosure and check the fit with calipers.
