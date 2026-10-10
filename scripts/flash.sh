#!/usr/bin/env bash
# First-time flash of a Sunton board over USB, then stay on the serial monitor.
#   scripts/flash.sh            autodetect the USB-serial port
#   scripts/flash.sh /dev/cu.X  use a specific port
#
# What you should see on the monitor after it resets:
#   podcast-jukebox 0.0.0-dev (built ...)
#   [net] setup hotspot Jukebox-Setup  key xxxxxxxxxxxx   <- first boot only: join it from
#                                                            a phone, pick your Wi-Fi
#   [net] <ssid>  ip 192.168.x.x  rssi -xx
#   [ota] running 0.0.0-dev, checking https://github.com/.../manifest.json
#   [ota] latest 0.1.0 (NNNN bytes)
#   [ota] ... KB   ...   flashed, rebooting into 0.1.0      <- the self-update
#   podcast-jukebox 0.1.0 ...  [ota] up to date
#   [channels] 20 channels
set -euo pipefail
cd "$(dirname "$0")/.."

port="${1:-}"
if [[ -z "$port" ]]; then
  for p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem*; do
    [[ -e "$p" ]] && { port="$p"; break; }
  done
fi
if [[ -z "$port" ]]; then
  echo "No USB-serial port found. Plug the board in (data cable, not charge-only) and run: ls /dev/cu.*" >&2
  echo "If nothing appears, the CH340 driver is missing: brew install --cask wch-ch34x-usb-serial-driver" >&2
  exit 1
fi
echo "Using $port"
# Guard: only flash a plain ESP32. On 2026-10-10 the autodetect picked the CrowPanel
# notetaker (ESP32-P4) because the Sunton was not enumerating; esptool refused, but
# make the refusal explicit and early.
chip=$(pio pkg exec -p tool-esptoolpy -- esptool.py --port "$port" --connect-attempts 3 chip_id 2>/dev/null | grep -m1 'Chip is' || true)
echo "${chip:-Chip is (unknown)}"
if [[ "$chip" != *"Chip is ESP32-D0WD"* && "$chip" != *"Chip is ESP32 "* && "$chip" != *"Chip is ESP32-D0WDQ"* ]]; then
  echo "Refusing: $port is not a plain ESP32 (Sunton 3248S035). Check which board is on this port." >&2
  exit 1
fi
FW_VERSION="${FW_VERSION:-}" pio run -t upload --upload-port "$port"
exec pio device monitor --port "$port" --baud 115200
