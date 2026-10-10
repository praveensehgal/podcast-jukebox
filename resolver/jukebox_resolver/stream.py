"""Audio pass-through for the device.

The ESP32 in the jukebox has no PSRAM. A TLS session to a podcast CDN cost it 56-75 KB
of heap (measured 2026-10-10), which left no room for the MP3 decoder, so it fetches
audio from here over plain HTTP and this service does the HTTPS, redirects and signed
URLs. Bounds: only URLs of cached episodes, STREAM_CONCURRENCY relays at once, and
STREAM_MAX_BYTES per relay.
"""

from __future__ import annotations

import asyncio
import logging
import re
from collections.abc import AsyncIterator, Awaitable, Callable

import httpx
from starlette.background import BackgroundTask
from starlette.responses import StreamingResponse

from .config import STREAM_CHUNK_BYTES, STREAM_MAX_BYTES
from .errors import ApiError
from .fetcher import Fetcher, FetchError

log = logging.getLogger("jukebox.stream")
RANGE_RE = re.compile(r"^bytes=(\d{1,12})-(\d{1,12})?$")
PASS_HEADERS = ("content-length", "content-range")
DEFAULT_TYPE = "audio/mpeg"


def check_range(header: str | None) -> str | None:
    """Accept only a single `bytes=start-` or `bytes=start-end` range (what resume sends)."""
    if header is None:
        return None
    match = RANGE_RE.match(header.strip())
    if not match or (match.group(2) and int(match.group(2)) < int(match.group(1))):
        raise ApiError(416, "range must be bytes=start- or bytes=start-end")
    return header.strip()


async def _open_first(
    fetcher: Fetcher, sources: list[str], range_header: str | None
) -> httpx.Response:
    """The first source that answers. A resolved URL can be an expired signed link."""
    last: FetchError = FetchError("no source for this episode")
    for url in sources:
        try:
            upstream = await fetcher.open_stream(url, range_header)
        except FetchError as exc:
            log.info("stream source failed: %s", exc)
            last = exc
            continue
        declared = upstream.headers.get("content-length", "")
        if declared.isdigit() and int(declared) > STREAM_MAX_BYTES:
            await upstream.aclose()
            raise FetchError(f"audio of {declared} bytes exceeds the {STREAM_MAX_BYTES} byte cap")
        return upstream
    raise last


Cleanup = Callable[[], Awaitable[None]]


def _once(upstream: httpx.Response, slots: asyncio.Semaphore) -> Cleanup:
    """Close the upstream and free the slot exactly once, whoever gets there first."""
    done = False

    async def cleanup() -> None:
        nonlocal done
        if done:
            return
        done = True
        await upstream.aclose()
        slots.release()

    return cleanup


async def _relay(upstream: httpx.Response, cleanup: Cleanup) -> AsyncIterator[bytes]:
    sent = 0
    try:
        async for chunk in upstream.aiter_raw(STREAM_CHUNK_BYTES):
            if sent + len(chunk) > STREAM_MAX_BYTES:
                log.info("stream stopped at the %d byte cap", STREAM_MAX_BYTES)
                break
            sent += len(chunk)
            yield chunk
    except httpx.HTTPError as exc:
        log.info("stream ended early after %d bytes: %s", sent, exc.__class__.__name__)
    finally:
        await cleanup()


def _headers(upstream: httpx.Response) -> dict[str, str]:
    out = {name: upstream.headers[name] for name in PASS_HEADERS if name in upstream.headers}
    kind = upstream.headers.get("content-type", "").split(";")[0].strip().lower()
    out["content-type"] = kind if kind.startswith("audio/") else DEFAULT_TYPE
    out["accept-ranges"] = "bytes"
    return out


async def relay_audio(
    fetcher: Fetcher, slots: asyncio.Semaphore, sources: list[str], range_header: str | None
) -> StreamingResponse:
    wanted = check_range(range_header)
    if slots.locked():
        raise ApiError(429, "too many streams in progress")
    await slots.acquire()
    try:
        upstream = await _open_first(fetcher, sources, wanted)
    except BaseException:
        slots.release()
        raise
    cleanup = _once(upstream, slots)
    return StreamingResponse(
        _relay(upstream, cleanup),
        status_code=upstream.status_code,
        headers=_headers(upstream),
        background=BackgroundTask(cleanup),  # also runs if the body was never iterated
    )
