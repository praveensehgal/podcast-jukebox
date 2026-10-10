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

Outbound requests use `User-Agent: podcast-jukebox-resolver/<version>`, a 15 s timeout
(8 s for redirect probes), at most 6 redirects, and refuse anything that is not http(s).
One log line per request goes to stdout: `GET /episodes 200 312.4ms`.

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

Newest-first episodes. `limit` is clamped to 1..50 (default 20). Cached 15 minutes per
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

Follow redirects right before playing (signed CDN URLs expire). HEAD first, then a GET
probe if the CDN rejects HEAD, else the input URL is returned unchanged.

```sh
curl -s --get localhost:8787/resolve --data-urlencode 'url=https://play.podtrac.com/npr-500005/....mp3'
# {"ok":true,"data":{"url":"https://ondemand.npr.org/...mp3"}}
```

### With a token

```sh
curl -s -H "X-Jukebox-Token: $JUKEBOX_TOKEN" localhost:8787/channels
```
