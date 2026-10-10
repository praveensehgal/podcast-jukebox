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


# -- third review round: request budget, refresh cap, hostile enclosures ------------


class SlowResolve(FakeFetcher):
    def __init__(self, slow: dict[str, float], **kw) -> None:
        super().__init__(**kw)
        self.slow = slow
        self.attempts: list[str] = []

    async def resolve(self, url: str) -> str:
        self.attempts.append(url)
        if url in self.slow:
            await asyncio.sleep(self.slow[url])
        return await super().resolve(url)


@pytest.mark.asyncio
async def test_request_budget_returns_partial_resolution(store, monkeypatch):
    monkeypatch.setattr(episodes_module, "EPISODES_REQUEST_BUDGET_S", 0.15)
    fetcher = SlowResolve(
        {MID: 2.0}, docs={FEED_URL: RSS}, resolves={NEW: "https://f/new", OLD: "https://f/old"}
    )
    svc = EpisodeService(fetcher, store)
    started = asyncio.get_running_loop().time()
    got = await svc.get(FEED_URL, 10)
    assert asyncio.get_running_loop().time() - started < 1.0
    assert [e["url"] for e in got] == ["https://f/new", MID, "https://f/old"]
    fetcher.slow.clear()
    got = await svc.get(FEED_URL, 10)  # the unresolved one is retried next time
    assert [e["url"] for e in got][1] == MID and fetcher.attempts.count(MID) == 2


@pytest.mark.asyncio
async def test_request_budget_covers_fetch_and_is_negative_cached(store, monkeypatch):
    monkeypatch.setattr(episodes_module, "EPISODES_REQUEST_BUDGET_S", 0.05)
    fetcher = FakeFetcher(docs={FEED_URL: RSS}, delay=0.5)
    svc = EpisodeService(fetcher, store)
    with pytest.raises(FetchError, match="budget"):
        await svc.get(FEED_URL, 3)
    with pytest.raises(FetchError, match="retry in"):
        await svc.get(FEED_URL, 3)
    assert fetcher.get_calls == [FEED_URL]


class CountingFetcher(FakeFetcher):
    def __init__(self, **kw) -> None:
        super().__init__(**kw)
        self.in_flight = 0
        self.peak = 0

    async def get_text(self, url: str, max_bytes: int = 0) -> str:
        self.in_flight += 1
        self.peak = max(self.peak, self.in_flight)
        try:
            return await super().get_text(url, max_bytes)
        finally:
            self.in_flight -= 1


@pytest.mark.asyncio
async def test_concurrent_refreshes_are_capped_service_wide(store, monkeypatch):
    monkeypatch.setattr(episodes_module, "MAX_CONCURRENT_REFRESHES", 2)
    feeds = [f"https://example.org/{i}.xml" for i in range(8)]
    fetcher = CountingFetcher(docs=dict.fromkeys(feeds, RSS), delay=0.02)
    svc = EpisodeService(fetcher, store)
    results = await asyncio.gather(*(svc.get(f, 1) for f in feeds))
    assert all(len(r) == 1 for r in results)
    assert fetcher.peak <= 2 and len(fetcher.get_calls) == 8


@pytest.mark.asyncio
async def test_queued_refresh_timeout_is_not_negative_cached(store, monkeypatch):
    monkeypatch.setattr(episodes_module, "MAX_CONCURRENT_REFRESHES", 1)
    monkeypatch.setattr(episodes_module, "EPISODES_REQUEST_BUDGET_S", 0.05)
    fetcher = FakeFetcher(docs={FEED_URL: RSS})
    svc = EpisodeService(fetcher, store)
    await svc._refreshes.acquire()  # another feed's fetch holds the only slot
    try:
        with pytest.raises(FetchError, match="budget"):
            await svc.get(FEED_URL, 1)
    finally:
        svc._refreshes.release()
    assert FEED_URL not in svc._failed  # it never started fetching, so no backoff
    assert fetcher.get_calls == []
    assert len(await svc.get(FEED_URL, 1)) == 1  # works once a slot is free


HOSTILE_RSS = """<rss version="2.0"><channel>
<item><title>meta</title><guid>m</guid><pubDate>Mon, 01 Sep 2025 10:05:00 +0000</pubDate>
  <enclosure url="http://169.254.169.254/latest/meta-data/" type="audio/mpeg"/></item>
<item><title>intranet</title><guid>i</guid><pubDate>Mon, 01 Sep 2025 10:04:00 +0000</pubDate>
  <enclosure url="http://internal.test/a.mp3" type="audio/mpeg"/></item>
<item><title>fine</title><guid>f</guid><pubDate>Mon, 01 Sep 2025 10:03:00 +0000</pubDate>
  <enclosure url="https://cdn.test/ok.mp3" type="audio/mpeg"/></item>
<item><title>ftp</title><guid>p</guid><pubDate>Mon, 01 Sep 2025 10:02:00 +0000</pubDate>
  <enclosure url="ftp://cdn.test/no.mp3" type="audio/mpeg"/></item>
<item><title>port</title><guid>q</guid><pubDate>Mon, 01 Sep 2025 10:01:00 +0000</pubDate>
  <enclosure url="https://cdn.test:8443/no.mp3" type="audio/mpeg"/></item>
<item><title>file</title><guid>r</guid><pubDate>Mon, 01 Sep 2025 10:00:00 +0000</pubDate>
  <enclosure url="file:///etc/passwd" type="audio/mpeg"/></item>
</channel></rss>"""


@pytest.mark.asyncio
async def test_hostile_enclosures_never_reach_the_transport(store):
    import httpx

    from jukebox_resolver.fetcher import Fetcher

    hosts_seen: list[str] = []

    def handler(request: httpx.Request) -> httpx.Response:
        hosts_seen.append(request.headers["host"])
        if request.url.path == "/feed.xml":
            return httpx.Response(200, stream=httpx.ByteStream(HOSTILE_RSS.encode()))
        if request.url.path == "/ok.mp3":
            return httpx.Response(302, headers={"location": "https://cdn.test/final.mp3"})
        return httpx.Response(200, stream=httpx.ByteStream(b""))

    resolver = {"cdn.test": ["93.184.216.34"], "internal.test": ["10.0.0.5"]}
    fetcher = Fetcher(transport=httpx.MockTransport(handler), resolver=lambda h: resolver[h])
    svc = EpisodeService(fetcher, store)
    got = await svc.get("https://cdn.test/feed.xml", 10)
    assert [e["title"] for e in got] == ["meta", "intranet", "fine"]  # ftp/port/file dropped
    assert got[0]["url"] == "http://169.254.169.254/latest/meta-data/"  # unresolved, scheme ok
    assert got[1]["url"] == "http://internal.test/a.mp3"
    assert got[2]["url"] == "https://cdn.test/final.mp3"
    assert set(hosts_seen) == {"cdn.test"}
    await fetcher.aclose()
