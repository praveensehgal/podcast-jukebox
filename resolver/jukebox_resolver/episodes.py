"""Episode lists per feed: fetch, parse, cache 15 minutes, resolve audio URLs on demand.

Bounds: one wall-clock budget per request (EPISODES_REQUEST_BUDGET_S) covering the
cache lookup, the upstream fetch and URL resolution; at most MAX_CONCURRENT_REFRESHES
upstream fetches service-wide; single-flight per feed; failures negative-cached for
NEGATIVE_TTL_S. When the budget runs out during resolution the request still answers,
with enclosure URLs for whatever is not resolved yet.
"""

from __future__ import annotations

import asyncio
import logging
import time
from collections import defaultdict
from typing import Any

from .config import (
    EPISODES_REQUEST_BUDGET_S,
    FEED_MAX_BYTES,
    FEED_REFRESH_BUDGET_S,
    FEED_TTL_S,
    MAX_CONCURRENT_REFRESHES,
    NEGATIVE_TTL_S,
)
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
        self._refreshes = asyncio.Semaphore(MAX_CONCURRENT_REFRESHES)

    async def get(self, feed: str, limit: int) -> list[Episode]:
        deadline = asyncio.get_running_loop().time() + EPISODES_REQUEST_BUDGET_S
        try:
            async with self._locks[feed]:
                try:
                    async with asyncio.timeout_at(deadline):
                        fetched_at, episodes = await self._entry(feed)
                except TimeoutError as exc:
                    raise FetchError(
                        f"request exceeded the {EPISODES_REQUEST_BUDGET_S:g} s budget"
                    ) from exc
                episodes = await self._resolve_slice(feed, fetched_at, episodes, limit, deadline)
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
            self._fail(feed, exc)
            if hit is not None:
                log.warning("feed refresh failed for %s (%s); serving stale copy", feed, exc)
                return hit
            raise

    def _fail(self, feed: str, exc: Exception) -> None:
        self._failed[feed] = (time.time() + NEGATIVE_TTL_S, exc)
        _prune(self._failed)

    async def _refresh(self, feed: str) -> Cached:
        async with self._refreshes:  # cancelled while queued: no negative cache entry
            try:
                async with asyncio.timeout(FEED_REFRESH_BUDGET_S):
                    xml_text = await self._fetcher.get_text(feed, FEED_MAX_BYTES)
            except TimeoutError as exc:
                raise FetchError(
                    f"feed refresh exceeded the {FEED_REFRESH_BUDGET_S} s budget"
                ) from exc
            except asyncio.CancelledError:
                self._fail(feed, FetchError("feed fetch exceeded the request budget"))
                raise
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
        self, feed: str, fetched_at: float, episodes: list[Episode], limit: int, deadline: float
    ) -> list[Episode]:
        """Resolve the requested, not-yet-resolved episodes until the deadline; persist."""
        pending = [i for i, ep in enumerate(episodes[:limit]) if ep.get("final") is None]
        if not pending:
            return episodes
        tasks = {asyncio.create_task(self._fetcher.resolve(episodes[i]["url"])): i for i in pending}
        remaining = max(0.0, deadline - asyncio.get_running_loop().time())
        done, not_done = await asyncio.wait(tasks, timeout=remaining)
        if not_done:
            log.warning("%s: budget hit, %d of %d URLs unresolved", feed, len(not_done), len(tasks))
            for task in not_done:
                task.cancel()
            await asyncio.gather(*not_done, return_exceptions=True)
        updated = list(episodes)
        for task in done:
            index = tasks[task]
            if task.exception() is None:
                updated[index] = {**episodes[index], "final": task.result()}
            else:
                log.info("%s: probe skipped for %s: %s", feed, episodes[index]["url"],
                         task.exception())
        self._store.put_feed(feed, updated, fetched_at)
        self._remember(feed, (fetched_at, updated))
        return updated
