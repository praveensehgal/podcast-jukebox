"""Episode lists per feed: fetch, parse, resolve audio URLs, cache 15 minutes."""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from .config import FEED_MAX_BYTES, FEED_TTL_S
from .feeds import FeedError, parse_feed
from .fetcher import Fetcher, FetchError
from .store import Store

log = logging.getLogger("jukebox.episodes")
RESOLVE_CONCURRENCY = 8

Cached = tuple[float, list[dict[str, Any]]]


class EpisodeService:
    def __init__(self, fetcher: Fetcher, store: Store, ttl_s: int = FEED_TTL_S) -> None:
        self._fetcher = fetcher
        self._store = store
        self._ttl = ttl_s
        self._mem: dict[str, Cached] = {}

    async def get(self, feed: str, limit: int) -> list[dict[str, Any]]:
        episodes = await self._episodes(feed)
        return episodes[:limit]

    async def _episodes(self, feed: str) -> list[dict[str, Any]]:
        now = time.time()
        hit = self._mem.get(feed)
        if hit is None:
            hit = self._store.get_feed(feed)
            if hit is not None:
                self._mem[feed] = hit
        if hit is not None and now - hit[0] < self._ttl:
            return hit[1]
        try:
            return await self._refresh(feed)
        except (FetchError, FeedError) as exc:
            if hit is not None:
                log.warning("feed refresh failed for %s (%s); serving stale copy", feed, exc)
                return hit[1]
            raise

    async def _refresh(self, feed: str) -> list[dict[str, Any]]:
        xml_text = await self._fetcher.get_text(feed, FEED_MAX_BYTES)
        parsed = parse_feed(feed, xml_text)
        episodes = await self._resolve_all(parsed)
        fetched_at = self._store.put_feed(feed, episodes)
        self._mem[feed] = (fetched_at, episodes)
        return episodes

    async def _resolve_all(self, episodes: list[dict[str, Any]]) -> list[dict[str, Any]]:
        gate = asyncio.Semaphore(RESOLVE_CONCURRENCY)

        async def one(episode: dict[str, Any]) -> dict[str, Any]:
            async with gate:
                final = await self._fetcher.resolve(episode["url"])
            return {**episode, "url": final}

        return list(await asyncio.gather(*(one(e) for e in episodes)))
