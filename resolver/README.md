# Podcast Jukebox resolver

A small HTTP service the ESP32 jukebox talks to instead of parsing RSS itself. It turns
podcast feeds into short JSON episode lists, follows CDN redirects to the final audio
URL, and remembers listening positions per device.

Python 3.12, FastAPI + uvicorn, SQLite (stdlib), httpx, defusedxml. No feedparser.

## Run on a Mac (dev)

```sh
cd resolver
uv sync                      # creates .venv with Python 3.12
uv run jukebox-resolver      # http://127.0.0.1:8787
# or:
uv run uvicorn jukebox_resolver.server:app --host 127.0.0.1 --port 8787 --no-access-log
```

Tests and lint (from the repo root):

```sh
uv run --project resolver pytest resolver/tests -q
uv run --project resolver ruff check resolver
(cd resolver && uv run pytest -q --cov=jukebox_resolver --cov-report=term-missing)  # keeps .coverage inside resolver/
resolver/scripts/smoke.sh    # live: starts a server, hits the NPR feed, stops it
```

## Run in Docker (prod, EC2)

```sh
cd resolver
printf 'JUKEBOX_TOKEN=%s\n' "$(openssl rand -hex 24)" > .env   # optional, never commit
docker compose up -d --build
curl -s http://127.0.0.1:8787/health
```

The container binds to `127.0.0.1:8787` only. Put it behind the box's reverse proxy on
443 and pass `X-Jukebox-Token` through. The database lives in the `jukebox-data` volume.

## Configuration (environment)

| Variable | Default | Meaning |
|---|---|---|
| `JUKEBOX_DB` | `./resolver.db` (`/data/resolver.db` in Docker) | SQLite file |
| `JUKEBOX_PORT` | `8787` | Port for `jukebox-resolver` / `python -m jukebox_resolver` |
| `JUKEBOX_HOST` | `127.0.0.1` | Bind address for the same entrypoint |
| `JUKEBOX_CHANNELS_URL` | GitHub `channels` release asset | Where `channels.md` is fetched from |
| `JUKEBOX_TOKEN` | unset | When set, every endpoint except `/health` needs `X-Jukebox-Token: <value>` |

Outbound requests use `User-Agent: podcast-jukebox-resolver/<version>`, 15 s connect and
read timeouts (8 s for redirect probes), and at most 6 redirects. One log line per request
goes to stdout: `GET /episodes 200 312.4ms`.

## Outbound URL policy (SSRF guard)

The input space is bounded first: `/episodes` only accepts feeds from `channels.md`
(at most 20) and `/resolve` only accepts URLs of episodes already cached for those
feeds (at most 50 each). Nothing outside that set is fetched, probed, locked or
negative-cached, so a client cannot fill the caches or the backoff table with
arbitrary URLs. In-memory tables are sized to that bound (64 feeds).

Every URL the service is asked to fetch (`feed`, `url`, the channels URL) and every
redirect `Location` it meets on the way is checked before any request is made:

- scheme must be `http` or `https`; port, if given, must be 80 or 443;
- the host (or each address it resolves to) must be public: loopback, link-local
  (169.254/16 including the cloud metadata address, fe80::/10), private (10/8,
  172.16/12, 192.168/16, 100.64/10, fc00::/7), multicast, reserved and unspecified
  addresses are refused, IPv4-mapped IPv6 included;
- a public host that redirects to a refused one stops the chain: `/resolve` returns the
  input URL unchanged and feed fetches fail with 502.

Refused input URLs answer 400 `url refused: <reason>`.

The connection is pinned to the address that passed the check: the host is resolved
once, the socket goes to that literal IP, and the `Host` header and TLS SNI carry the
original hostname so certificates still verify. A second DNS answer (DNS rebinding)
cannot move the connection.

## Resource limits

- Response bodies are read from the raw stream and refused before any byte past the
  cap is buffered or decoded: feeds 5 MB, `channels.md` 256 KB. `Accept-Encoding:
  identity` is requested; gzip/deflate bodies a server sends anyway are inflated with the
  same cap applied to the decoded size, other encodings are refused. Oversize is a 502.
- At most 6 redirect hops per fetch or probe; 15 s connect/read timeouts; 8 s per probe.
- Time budgets: a feed or channels GET including redirects 30 s, one `/resolve` call
  20 s, a feed refresh 35 s. Past the budget: 502 for fetches, the input URL for
  `/resolve`.
- Redirect probes are limited to 8 in flight service-wide, and only the episodes a
  request actually returns are resolved (results are cached with the feed), so a
  `limit=50` request costs at most 50 probes once per 15 minutes.
- Every `/episodes` request has a 20 s wall-clock budget covering the cache lookup, the
  upstream fetch and URL resolution. If resolution is still running at the deadline the
  request answers anyway with enclosure URLs for the unresolved episodes; they are
  retried on the next request. At most 4 upstream feed fetches run at once service-wide;
  a request that never got a fetch slot inside its budget gets a 502 without a backoff
  entry.
- Enclosure URLs from feeds go through the same policy as everything else: episodes
  whose enclosure is not http(s) on port 80/443 are dropped at parse time, and a probe
  of an enclosure whose host is not public is refused before any connection (the
  episode is returned with its enclosure URL unchanged).
- A feed that fails (unreachable, oversize, unparseable, refused) is negative-cached
  for 60 s: repeated device requests get the same 502 (or 400) immediately, with no
  new upstream fetch; a stale copy, if any, is served instead. Refreshes are
  single-flight per feed, so concurrent requests share one upstream fetch, and other
  feeds are never blocked by a slow one.

## API

Every response is JSON wrapped as `{"ok": true, "data": ...}` or
`{"ok": false, "error": "message"}`. Status codes: 200 ok, 400 bad input, 401 bad or
missing token, 404 unknown route, 502 upstream feed/CDN failure, 500 bug.

### `GET /health`

```sh
curl -s localhost:8787/health
# {"ok":true,"data":{"version":"0.1.0","channels":20,"uptime_s":42}}
```

`channels` is the number of rows in the cached channel list (0 until the first
`/channels` call after a cold start without a saved copy).

### `GET /channels`

The table in `channels.md`, cached 10 minutes, last good copy served if GitHub is down.

```sh
curl -s localhost:8787/channels
# {"ok":true,"data":[{"position":1,"name":"NPR News Now","feed":"https://feeds.npr.org/500005/podcast.xml"}, ...]}
```

### `GET /episodes?feed=<url>&limit=20`

Newest-first episodes. `feed` must be one of the URLs in the current channel list
(exact match); anything else is 404 `feed is not in the channel list` and is never
fetched or cached. `limit` is clamped to 1..50 (default 20). Cached 15 minutes per
feed in memory and in SQLite, so a restart is not cold; a stale copy is served if the
feed is unreachable.

```sh
curl -s --get localhost:8787/episodes --data-urlencode 'feed=https://feeds.npr.org/500005/podcast.xml' --data-urlencode 'limit=3'
```

Each item:

| Field | Type | Notes |
|---|---|---|
| `id` | string(16) | `sha1(feed + "|" + guid-or-enclosure-url)[:16]`, stable across fetches |
| `title` | string | HTML stripped, whitespace collapsed, at most 200 chars, `Untitled` if empty |
| `published` | string | ISO 8601 UTC, `2025-09-03T10:30:00Z` |
| `duration_s` | int or null | from `itunes:duration` (`HH:MM:SS`, `MM:SS` or seconds) |
| `url` | string | final audio URL after redirects (falls back to the enclosure URL) |
| `mime` | string or null | enclosure `type` |
| `size` | int or null | enclosure `length`, null when missing or 0 |

Episodes with no enclosure or an unparseable date are skipped.

### `GET /position?device=<id>&episode=<id>`

```sh
curl -s 'localhost:8787/position?device=esp32-01&episode=3f2a9c1e8b7d6a54'
# {"ok":true,"data":{"pos_s":312,"duration_s":1800,"updated":"2026-10-10T14:03:11Z"}}
# never played: {"ok":true,"data":{"pos_s":0,"duration_s":null,"updated":null}}
```

### `POST /position`

Upsert. `device` and `episode` are 1..64 chars of `[A-Za-z0-9:_-]`; `pos_s` is an int
>= 0; `duration_s` is an int >= 0 or omitted (an omitted duration keeps the stored one).

```sh
curl -s -X POST localhost:8787/position -H 'Content-Type: application/json' \
  -d '{"device":"esp32-01","episode":"3f2a9c1e8b7d6a54","pos_s":312,"duration_s":1800}'
```

### `GET /positions?device=<id>`

All positions for a device, most recently updated first, for NEW/progress badges.

```sh
curl -s 'localhost:8787/positions?device=esp32-01'
# {"ok":true,"data":[{"episode":"3f2a9c1e8b7d6a54","pos_s":312,"duration_s":1800,"updated":"..."}]}
```

### `GET /resolve?url=<audio url>`

Follow redirects right before playing (signed CDN URLs expire). `url` must be the `url`
of an episode the service currently has cached for a channel-list feed (the enclosure
URL or an earlier resolved URL both work); anything else is 404 `url is not a known
episode`. HEAD first, then a GET probe if the CDN rejects HEAD, else the input URL is
returned unchanged.

```sh
curl -s --get localhost:8787/resolve --data-urlencode 'url=https://play.podtrac.com/npr-500005/....mp3'
# {"ok":true,"data":{"url":"https://ondemand.npr.org/...mp3"}}
```

### With a token

```sh
curl -s -H "X-Jukebox-Token: $JUKEBOX_TOKEN" localhost:8787/channels
```
