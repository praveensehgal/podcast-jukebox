# 005 - Audio reaches the device through the resolver, over plain HTTP

**Date:** 2026-10-10
**Status:** accepted
**Context:** The device was fetching episode audio straight from podcast CDNs over HTTPS. On 2026-10-10 the first tap-to-play of the touch build aborted, and the committed firmware from that morning did the same. Measured on the bench board (ESP32-WROOM, no PSRAM), in byte-addressable heap:

| Item | Size |
|---|---|
| Free before a stream opens | about 105 KB |
| One TLS session to a podcast CDN | 56 to 75 KB, varying per connection and per CDN |
| MP3 decoder (Helix tables and buffers) | about 34 KB |
| Ring buffer | 16 KB |

TLS plus decoder plus ring is 106 to 125 KB. It does not fit. It had appeared to work earlier only because the library does not report a failed decoder allocation: the stream was read at 27 KB/s (1.7 times real time) with nothing decoded. Through the relay the same episode reads at 16 KB/s, which is 128 kbps in real time.
**Decision:** The resolver gets `GET /stream?url=<episode url>`. It fetches the audio (HTTPS, redirects, signed URLs, a Range header for resume) and relays the bytes; the device plays `http://<resolver>/stream?...` and never opens a TLS session for audio. The endpoint only serves URLs of episodes already cached for a channel-list feed, tries the resolved URL and then the enclosure (an expired signed link falls back), holds at most four relays at once, and stops at 1 GiB per relay. TLS is still used at boot for the firmware manifest and the channel list, when nothing else is allocated.
**Alternatives:** Shrinking other memory (the LVGL draw buffer went from 37.5 KB to 9.4 KB, which stays, but the worst-case sum still does not fit). Rebuilding the Arduino core with mbedTLS dynamic buffers (saves perhaps 20 KB, turns every build into a from-source framework compile, and the margin stays thin). A board with PSRAM (the right answer for a second unit; this one is the board in hand).
**Consequences:** Playback depends on the resolver being reachable, which the episode lists already did. Audio is public podcast content, so plain HTTP exposes nothing private, but the relay must sit behind the same allow-list and limits as the rest of the service wherever it is hosted; on EC2 it will carry a few GB of traffic a month. With the stream open the device now has about 85 KB free instead of 6 to 21 KB, so position saves and screen changes during playback no longer compete for memory.
