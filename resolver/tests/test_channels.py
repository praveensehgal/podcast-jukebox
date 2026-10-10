import pytest

from conftest import CHANNELS_MD, CHANNELS_URL, FakeFetcher
from jukebox_resolver.channels import ChannelService, parse_channels
from jukebox_resolver.fetcher import FetchError
from jukebox_resolver.store import Store


def test_parse_orders_by_position_and_skips_bad_rows():
    rows = parse_channels(CHANNELS_MD)
    assert [r["position"] for r in rows] == [1, 2, 5]
    assert rows[0] == {
        "position": 1,
        "name": "NPR News Now",
        "feed": "https://feeds.npr.org/500005/podcast.xml",
    }
    assert rows[2]["name"] == "The Daily"


def test_parse_ignores_header_and_prose():
    assert parse_channels("# nothing\n|---|---|---|\n| # | Channel | Feed |\n") == []


def test_parse_last_duplicate_wins():
    md = "| 1 | A | https://a |\n| 1 | B | https://b |\n"
    assert parse_channels(md) == [{"position": 1, "name": "B", "feed": "https://b"}]


@pytest.mark.asyncio
async def test_service_caches_and_persists(tmp_path):
    store = Store(str(tmp_path / "c.db"))
    fetcher = FakeFetcher(docs={CHANNELS_URL: CHANNELS_MD})
    svc = ChannelService(fetcher, store, CHANNELS_URL)
    assert svc.cached_count() == 0
    first = await svc.get()
    second = await svc.get()
    assert first == second and len(first) == 3
    assert fetcher.get_calls == [CHANNELS_URL]
    assert svc.cached_count() == 3
    # A fresh service on the same DB starts from the last good copy.
    cold = ChannelService(FakeFetcher(), store, CHANNELS_URL, ttl_s=0)
    assert cold.cached_count() == 3
    assert await cold.get() == first  # upstream fails, last good served
    store.close()


@pytest.mark.asyncio
async def test_service_errors_when_nothing_cached(tmp_path):
    store = Store(str(tmp_path / "c.db"))
    svc = ChannelService(FakeFetcher(), store, CHANNELS_URL)
    with pytest.raises(FetchError):
        await svc.get()
    store.close()


@pytest.mark.asyncio
async def test_service_rejects_empty_table(tmp_path):
    store = Store(str(tmp_path / "c.db"))
    svc = ChannelService(FakeFetcher(docs={CHANNELS_URL: "# empty"}), store, CHANNELS_URL)
    with pytest.raises(FetchError):
        await svc.get()
    store.close()
