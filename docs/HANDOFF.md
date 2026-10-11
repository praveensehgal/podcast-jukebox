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
- The soldering guide's pad map. It is drawn from the ESP32-WROOM-32 datasheet pin order
  and the earlier socket pinout notes, not from a photo of this board; its continuity
  checks (pad 3 to the IO22 wire, pad 6 to the IO21 wire) are what confirm it on the bench.
- The three physical switches, and whether GPIO 18/19/23 behave with real switches.
- The enclosure, as a print. enclosure/out now holds the two-part design (decision 006):
  enclosure_d_body.3mf (body on its back, face plate face down, switch bracket) and
  enclosure_d_buttons.3mf (three caps and the stem jig), from enclosure/build_d.py with
  WALL = 1.2 (prototype skin; 2.4 for the final). The script checks by computation that
  every part clears every other when assembled, that the plate swings in bottom-first
  without touching, and that the floor screws get 5 mm of thread; all of that passed on
  2026-10-11. Nothing has been sliced or printed. The parts to buy nothing for: M3 x 6
  screws (speaker x4, bracket x2, board x4) and M3 x 8 (floor x2) from the M3 kit.
- The numbers the CAD rests on. Measured 2026-10-10 from Praveen's caliper photo:
  speaker depth 28.22 mm, square frame with four corner holes (label XLY 4 ohm 3 W).
  Read off a photo against the cutting mat, not calipered: frame about 50 mm square,
  holes about 41 mm apart, magnet about 40 mm diameter and 13 mm thick. Nominal, and the
  first things the fit-test print will contradict: the Sunton's mounting holes (drawn
  3 mm in from the PCB corners, 101.5 x 54.9 outline), its 9.8 mm stack, the active area
  centred on the PCB; switch body 4.3 mm and stem 3.5 mm (caps have a 3.7 mm socket);
  amp holes at 15.24 mm pitch. With the drawn numbers the magnet clears the board by
  3.7 mm in the prototype (2.5 with final walls) and the switch legs clear it by 3.5.
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

1. Solder the MAX98357A and speaker and listen: docs/soldering-guide.html, Job A. DIN goes
   to the ESP32 module's edge pad for IO16 (12th from the antenna on the IO22 side).
2. Print the fit test: enclosure_d_body.3mf (no supports) and enclosure_d_buttons.3mf
   (print sequence by object). Check: the Sunton on its posts and through the window, the
   speaker on its seat, the bracket on its posts with switches in the pockets, the caps
   on cut stems, the plate's tongues and screws. Correct geom.py from what is off (the
   Sunton's hole positions first) and rebuild.
3. Solder the three switches to the module's edge pads for IO18/19/23 (same guide, Job B)
   once the carrier exists, so the harness is cut to length. A breadboard holds the
   switches for an earlier test.
4. Exercise the keyboard join and the phone portal from the Wi-Fi screen.
5. KCX module on arrival: version probe, then the driver branch.
6. Resolver to EC2 with a token, then cut `v0.2.0` (the first release that can play).
7. Final print with WALL = 2.4 once the fit test passes.
