"""Episode lists per feed: fetch, parse, cache 15 minutes, resolve audio URLs on demand.

Failures are negative-cached for NEGATIVE_TTL_S so an unreachable feed does not cost an
upstream round trip per device request; refreshes are single-flight per feed and bounded
by FEED_REFRESH_BUDGET_S.
"""

from __future__ import annotations

import asyncio
import logging
import time
from collections import defaultdict
from typing import Any

from .config import FEED_MAX_BYTES, FEED_REFRESH_BUDGET_S, FEED_TTL_S, NEGATIVE_TTL_S
from .feeds import FeedError, parse_feed
from .fetcher import BlockedUrlError, Fetcher, FetchError
from .store import Store

log = logging.getLogger("jukebox.episodes")
MAX_TRACKED_FEEDS = 200

Episode = dict[str, Any]
Cached = tuple[float, list[Episode]]
Failure = tuple[float, Exception]


def _internal(parsed: list[Episode]) -> list[Episode]:
    """Keep the enclosure URL; `final` is filled lazily by resolution."""
    return [{**ep, "final": None} for ep in parsed]


def _public(episodes: list[Episode]) -> list[Episode]:
    return [
        {k: v for k, v in ep.items() if k != "final"} | {"url": ep.get("final") or ep["url"]}
        for ep in episodes
    ]


def _prune(mapping: dict[str, Any]) -> None:
    while len(mapping) > MAX_TRACKED_FEEDS:
        del mapping[next(iter(mapping))]


class EpisodeService:
    def __init__(self, fetcher: Fetcher, store: Store, ttl_s: int = FEED_TTL_S) -> None:
        self._fetcher = fetcher
        self._store = store
        self._ttl = ttl_s
        self._mem: dict[str, Cached] = {}
        self._failed: dict[str, Failure] = {}
        self._locks: defaultdict[str, asyncio.Lock] = defaultdict(asyncio.Lock)

    async def get(self, feed: str, limit: int) -> list[Episode]:
        try:
            async with self._locks[feed]:
                fetched_at, episodes = await self._entry(feed)
                episodes = await self._resolve_slice(feed, fetched_at, episodes, limit)
        finally:
            self._prune_locks()
        return _public(episodes[:limit])

    def _prune_locks(self) -> None:
        for feed in list(self._locks):
            if len(self._locks) <= MAX_TRACKED_FEEDS:
                break
            if not self._locks[feed].locked():
                del self._locks[feed]

    async def _entry(self, feed: str) -> Cached:
        now = time.time()
        hit = self._mem.get(feed)
        if hit is None:
            hit = self._store.get_feed(feed)
            if hit is not None:
                self._remember(feed, hit)
        if hit is not None and now - hit[0] < self._ttl:
            return hit
        failure = self._failed.get(feed)
        if failure is not None and now < failure[0]:
            if hit is not None:
                return hit
            retry = int(failure[0] - now) + 1
            raise type(failure[1])(f"{failure[1]} (retry in {retry} s)")
        try:
            return await self._refresh(feed)
        except (FetchError, FeedError, BlockedUrlError) as exc:
            self._failed[feed] = (now + NEGATIVE_TTL_S, exc)
            _prune(self._failed)
            if hit is not None:
                log.warning("feed refresh failed for %s (%s); serving stale copy", feed, exc)
                return hit
            raise

    async def _refresh(self, feed: str) -> Cached:
        try:
            async with asyncio.timeout(FEED_REFRESH_BUDGET_S):
                xml_text = await self._fetcher.get_text(feed, FEED_MAX_BYTES)
        except TimeoutError as exc:
            raise FetchError(f"feed refresh exceeded the {FEED_REFRESH_BUDGET_S} s budget") from exc
        episodes = _internal(parse_feed(feed, xml_text))
        fetched_at = self._store.put_feed(feed, episodes)
        self._failed.pop(feed, None)
        entry = (fetched_at, episodes)
        self._remember(feed, entry)
        return entry

    def _remember(self, feed: str, entry: Cached) -> None:
        self._mem[feed] = entry
        _prune(self._mem)

    async def _resolve_slice(
        self, feed: str, fetched_at: float, episodes: list[Episode], limit: int
    ) -> list[Episode]:
        """Resolve only the requested, not-yet-resolved episodes; persist the result."""
        pending = [i for i, ep in enumerate(episodes[:limit]) if ep.get("final") is None]
        if not pending:
            return episodes
        finals = await asyncio.gather(
            *(self._fetcher.resolve(episodes[i]["url"]) for i in pending),
            return_exceptions=True,
        )
        updated = list(episodes)
        for index, final in zip(pending, finals, strict=True):
            if isinstance(final, str):
                updated[index] = {**episodes[index], "final": final}
        self._store.put_feed(feed, updated, fetched_at)
        self._remember(feed, (fetched_at, updated))
        return updated
