# 002 - Bluetooth out through a KCX_BT_EMITTER module, not the ESP32 radio

**Date:** 2026-10-10
**Status:** accepted
**Context:** Bluetooth means sending sound out to his headphones or hearing aids. The ESP32 on the Sunton board already has to stream over Wi-Fi, decode MP3 and drive a 480 × 320 LVGL screen. Hobbyist builds that add an A2DP source on the same radio stutter.
**Decision:** Audio leaves the ESP32 over I2S to two sinks: a MAX98357A for the built-in speaker and a PCM5102A DAC whose line-out feeds a KCX_BT_EMITTER transmitter. The ESP32 drives the module over UART (AT commands: scan, connect by address, status), which is what makes the on-screen device list possible. "Sound output" mutes the MAX98357A via its SD pin when Bluetooth is selected. Paired headphones reconnect automatically on power-up.
**Alternatives:** ESP32 A2DP source (rejected: radio contention with Wi-Fi streaming, and the WROOM has no PSRAM to buffer through dropouts). A second ESP32 as a Bluetooth bridge (rejected: more firmware for the same result as an $8 module).
**Consequences:** One new part (~$8). The Bluetooth screen depends on the module's AT responses, which must be verified on the bench before the UI is finalised.
