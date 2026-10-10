import asyncio

import pytest

from conftest import FEED_URL, RSS, FakeFetcher
from jukebox_resolver import episodes as episodes_module
from jukebox_resolver.episodes import EpisodeService
from jukebox_resolver.fetcher import BlockedUrlError, FetchError
from jukebox_resolver.store import Store

NEW, MID, OLD = (
    "https://cdn.example.org/new.mp3",
    "https://cdn.example.org/mid.mp3",
    "https://cdn.example.org/old.mp3",
)


@pytest.fixture
def store(tmp_path):
    s = Store(str(tmp_path / "e.db"))
    yield s
    s.close()


@pytest.mark.asyncio
async def test_resolves_only_requested_slice_and_caches(store):
    fetcher = FakeFetcher(docs={FEED_URL: RSS}, resolves={NEW: "https://f/new", OLD: "https://f/old"})
    svc = EpisodeService(fetcher, store)
    first = await svc.get(FEED_URL, 1)
    assert [e["url"] for e in first] == ["https://f/new"]
    assert fetcher.resolve_calls == [NEW]
    assert "final" not in first[0] and set(first[0]) >= {"id", "title", "url", "mime"}
    full = await svc.get(FEED_URL, 10)
    assert [e["url"] for e in full] == ["https://f/new", MID, "https://f/old"]
    assert fetcher.resolve_calls == [NEW, MID, OLD]  # no re-resolution of the first
    await svc.get(FEED_URL, 10)
    assert len(fetcher.resolve_calls) == 3 and fetcher.get_calls == [FEED_URL]
    # A fresh service on the same DB keeps the resolved URLs without touching the network.
    cold = EpisodeService(FakeFetcher(), store)
    assert [e["url"] for e in await cold.get(FEED_URL, 10)] == [e["url"] for e in full]


@pytest.mark.asyncio
async def test_failed_probe_keeps_enclosure_url(store):
    fetcher = FakeFetcher(docs={FEED_URL: RSS}, fail_resolve={NEW})
    svc = EpisodeService(fetcher, store)
    assert (await svc.get(FEED_URL, 1))[0]["url"] == NEW


@pytest.mark.asyncio
async def test_negative_cache_stops_repeated_upstream_fetches(store, monkeypatch):
    fetcher = FakeFetcher()
    svc = EpisodeService(fetcher, store)
    with pytest.raises(FetchError):
        await svc.get("https://example.org/down.xml", 5)
    with pytest.raises(FetchError, match="retry in"):
        await svc.get("https://example.org/down.xml", 5)
    assert fetcher.get_calls == ["https://example.org/down.xml"]
    monkeypatch.setattr(episodes_module.time, "time", lambda: 10_000_000_000.0)
    with pytest.raises(FetchError):
        await svc.get("https://example.org/down.xml", 5)
    assert len(fetcher.get_calls) == 2


@pytest.mark.asyncio
async def test_blocked_feed_is_negative_cached_as_400(store):
    class Blocking(FakeFetcher):
        async def get_text(self, url, max_bytes=0):
            self.get_calls.append(url)
            raise BlockedUrlError("host x is not a public address")

    fetcher = Blocking()
    svc = EpisodeService(fetcher, store)
    for _ in range(3):
        with pytest.raises(BlockedUrlError):
            await svc.get("https://evil.example/feed", 5)
    assert len(fetcher.get_calls) == 1


@pytest.mark.asyncio
async def test_stale_copy_served_without_refetch_during_backoff(store):
    fetcher = FakeFetcher(docs={FEED_URL: RSS})
    svc = EpisodeService(fetcher, store, ttl_s=0)
    good = await svc.get(FEED_URL, 5)
    fetcher.docs.clear()
    for _ in range(3):
        assert await svc.get(FEED_URL, 5) == good
    assert fetcher.get_calls == [FEED_URL, FEED_URL]  # one refresh attempt, then backoff


@pytest.mark.asyncio
async def test_single_flight_per_feed(store):
    fetcher = FakeFetcher(docs={FEED_URL: RSS}, delay=0.05)
    svc = EpisodeService(fetcher, store)
    results = await asyncio.gather(*(svc.get(FEED_URL, 3) for _ in range(5)))
    assert all(r == results[0] for r in results)
    assert fetcher.get_calls == [FEED_URL]


@pytest.mark.asyncio
async def test_refresh_budget(store, monkeypatch):
    monkeypatch.setattr(episodes_module, "FEED_REFRESH_BUDGET_S", 0.02)
    fetcher = FakeFetcher(docs={FEED_URL: RSS}, delay=0.3)
    svc = EpisodeService(fetcher, store)
    with pytest.raises(FetchError, match="budget"):
        await svc.get(FEED_URL, 3)


@pytest.mark.asyncio
async def test_tracking_tables_are_bounded(store, monkeypatch):
    monkeypatch.setattr(episodes_module, "MAX_TRACKED_FEEDS", 3)
    fetcher = FakeFetcher()
    svc = EpisodeService(fetcher, store)
    for i in range(10):
        with pytest.raises(FetchError):
            await svc.get(f"https://example.org/{i}.xml", 1)
    assert len(svc._failed) <= 3 and len(svc._locks) <= 3
