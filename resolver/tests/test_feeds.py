import pytest

from conftest import ATOM, FEED_URL, RSS
from jukebox_resolver.feeds import (
    FeedError,
    clean_title,
    episode_id,
    parse_date,
    parse_duration,
    parse_feed,
)


def test_rss_newest_first_and_skips():
    eps = parse_feed(FEED_URL, RSS)
    assert [e["title"] for e in eps] == [
        "Newest episode",
        "Middle, no guid",
        "Older & bold episode",
    ]
    assert eps[0]["published"] == "2025-09-03T10:30:00Z"
    assert eps[0]["duration_s"] == 754
    assert eps[0]["size"] is None  # length="0"
    assert eps[1]["size"] == 99
    assert eps[2]["duration_s"] == 3723
    assert eps[2]["mime"] == "audio/mpeg"
    assert eps[2]["url"] == "https://cdn.example.org/old.mp3"


def test_rss_ids_are_stable_and_derived_from_guid_or_url():
    eps = parse_feed(FEED_URL, RSS)
    assert eps[0]["id"] == episode_id(FEED_URL, "guid-new")
    assert eps[1]["id"] == episode_id(FEED_URL, "https://cdn.example.org/mid.mp3")
    assert eps == parse_feed(FEED_URL, RSS)
    assert episode_id(FEED_URL, "guid-new") != episode_id("https://other/feed", "guid-new")
    assert len(eps[0]["id"]) == 16


def test_atom_entries():
    eps = parse_feed("https://example.org/atom", ATOM)
    assert [e["title"] for e in eps] == ["Second atom", "First atom"]
    assert eps[0]["published"] == "2025-09-02T08:00:00Z"
    assert eps[0]["mime"] == "audio/mp4"
    assert eps[0]["size"] is None
    assert eps[1]["size"] == 555
    assert eps[1]["id"] == episode_id("https://example.org/atom", "urn:uuid:one")


def test_feed_errors():
    with pytest.raises(FeedError):
        parse_feed(FEED_URL, "<html><body>nope</body></html>")
    with pytest.raises(FeedError):
        parse_feed(FEED_URL, "this is not xml <")


def test_rss_without_channel_yields_nothing():
    assert parse_feed(FEED_URL, '<rss version="2.0"></rss>') == []


@pytest.mark.parametrize(
    ("raw", "expected"),
    [
        ("01:02:03", 3723),
        ("1:02:03", 3723),
        ("12:34", 754),
        ("754", 754),
        ("754.9", 754),
        ("  90 ", 90),
        ("", None),
        (None, None),
        ("abc", None),
        ("1:2:3:4", None),
        ("-5", None),
    ],
)
def test_parse_duration(raw, expected):
    assert parse_duration(raw) == expected


@pytest.mark.parametrize(
    ("raw", "expected"),
    [
        ("Wed, 03 Sep 2025 06:30:00 -0400", "2025-09-03T10:30:00Z"),
        ("Wed, 03 Sep 2025 06:30:00 GMT", "2025-09-03T06:30:00Z"),
        ("2025-09-03T06:30:00Z", "2025-09-03T06:30:00Z"),
        ("2025-09-03T06:30:00+02:00", "2025-09-03T04:30:00Z"),
        ("2025-09-03 06:30:00", "2025-09-03T06:30:00Z"),
    ],
)
def test_parse_date_ok(raw, expected):
    parsed = parse_date(raw)
    assert parsed is not None
    assert parsed.strftime("%Y-%m-%dT%H:%M:%SZ") == expected


@pytest.mark.parametrize("raw", [None, "", "yesterday", "3 days ago", "Sep 2025"])
def test_parse_date_rejects_unknown(raw):
    assert parse_date(raw) is None


def test_clean_title():
    assert clean_title("  <p>Hello&nbsp;&amp; <em>world</em></p>\n  x ") == "Hello & world x"
    assert clean_title(None) == "Untitled"
    assert clean_title("<br/>") == "Untitled"
    assert len(clean_title("a" * 500)) == 200


def test_episode_cap_at_50():
    items = "".join(
        f"<item><title>e{i}</title><guid>g{i}</guid>"
        f"<pubDate>Mon, 01 Sep 2025 10:{i:02d}:00 +0000</pubDate>"
        f'<enclosure url="https://cdn/e{i}.mp3" type="audio/mpeg"/></item>'
        for i in range(60)
    )
    eps = parse_feed(FEED_URL, f'<rss version="2.0"><channel>{items}</channel></rss>')
    assert len(eps) == 50
    assert eps[0]["title"] == "e59"


def test_non_http_enclosures_are_dropped():
    items = "".join(
        f"<item><title>{i}</title><guid>{i}</guid>"
        f"<pubDate>Mon, 01 Sep 2025 10:00:00 +0000</pubDate>"
        f'<enclosure url="{u}" type="audio/mpeg"/></item>'
        for i, u in enumerate(
            ["ftp://a/x.mp3", "file:///x.mp3", "https://a:8443/x.mp3", "javascript:1", "https://a/ok.mp3"]
        )
    )
    eps = parse_feed(FEED_URL, f'<rss version="2.0"><channel>{items}</channel></rss>')
    assert [e["url"] for e in eps] == ["https://a/ok.mp3"]
    atom = (
        '<feed xmlns="http://www.w3.org/2005/Atom"><entry><id>a</id><title>t</title>'
        '<updated>2025-09-01T10:00:00Z</updated><link rel="enclosure" href="ftp://a/x"/></entry></feed>'
    )
    assert parse_feed(FEED_URL, atom) == []


def test_a_feed_the_size_of_the_daily_fits_the_cap_and_parses():
    # The Daily's feed measured 20,574,634 bytes on 2026-10-29 and was refused by the old
    # 5 MB cap, so the channel showed no episodes. Build one past that size and parse it.
    from jukebox_resolver.config import FEED_MAX_BYTES, MAX_EPISODES

    item = (
        "<item><title>Episode {n}</title><guid>ep-{n}</guid>"
        "<pubDate>Wed, 03 Sep 2025 10:30:00 GMT</pubDate>"
        '<enclosure url="https://cdn.example.org/{n}.mp3" length="1" type="audio/mpeg"/>'
        "<description>{pad}</description></item>"
    )
    pad = "x" * 2000
    body = "".join(item.format(n=n, pad=pad) for n in range(10_500))
    xml = f'<?xml version="1.0"?><rss version="2.0"><channel>{body}</channel></rss>'
    assert len(xml) > 21_000_000
    assert len(xml) <= FEED_MAX_BYTES
    assert len(parse_feed(FEED_URL, xml)) == MAX_EPISODES
