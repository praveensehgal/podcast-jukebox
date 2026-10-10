"""Channel list: parse channels.md and serve it with a 10 minute cache."""

from __future__ import annotations

import logging
import re
import time
from typing import Any

from .config import CHANNELS_MAX_BYTES, CHANNELS_TTL_S
from .fetcher import Fetcher, FetchError
from .store import Store

log = logging.getLogger("jukebox.channels")

MAX_CHANNELS = 20
KV_KEY = "channels_md"
ROW_RE = re.compile(r"^\|\s*(\d{1,2})\s*\|\s*(.*?)\s*\|\s*(https?://\S+)\s*\|")


def parse_channels(markdown: str) -> list[dict[str, Any]]:
    """Rows `| n | name | https://feed |`, n in 1..20, returned in order of n."""
    found: dict[int, dict[str, Any]] = {}
    for raw in markdown.splitlines():
        match = ROW_RE.match(raw.strip())
        if not match:
            continue
        position = int(match.group(1))
        name = match.group(2).strip()
        if not 1 <= position <= MAX_CHANNELS or not name:
            continue
        found[position] = {"position": position, "name": name, "feed": match.group(3)}
    return [found[k] for k in sorted(found)]


class ChannelService:
    def __init__(self, fetcher: Fetcher, store: Store, url: str, ttl_s: int = CHANNELS_TTL_S):
        self._fetcher = fetcher
        self._store = store
        self._url = url
        self._ttl = ttl_s
        self._cached: tuple[float, list[dict[str, Any]]] | None = None
        self._load_last_good()

    def _load_last_good(self) -> None:
        saved = self._store.get_kv(KV_KEY)
        if saved:
            self._cached = (saved[0], parse_channels(saved[1]))

    def cached_count(self) -> int:
        return len(self._cached[1]) if self._cached else 0

    async def get(self) -> list[dict[str, Any]]:
        now = time.time()
        if self._cached and now - self._cached[0] < self._ttl:
            return self._cached[1]
        try:
            channels = await self._refresh()
        except (FetchError, ValueError) as exc:
            if self._cached:
                log.warning("channels refresh failed (%s); serving last good copy", exc)
                return self._cached[1]
            raise FetchError(f"channel list unavailable: {exc}") from exc
        return channels

    async def _refresh(self) -> list[dict[str, Any]]:
        text = await self._fetcher.get_text(self._url, CHANNELS_MAX_BYTES)
        channels = parse_channels(text)
        if not channels:
            raise ValueError("no channel rows found")
        self._store.put_kv(KV_KEY, text)
        self._cached = (time.time(), channels)
        return channels
