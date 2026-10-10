"""Shared fixtures: inline feed documents and a network-free fetcher."""

from __future__ import annotations

import pytest

FEED_URL = "https://example.org/feed.xml"
CHANNELS_URL = "https://example.org/channels.md"

RSS = """<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:itunes="http://www.itunes.com/dtds/podcast-1.0.dtd">
<channel>
  <title>Test Show</title>
  <item>
    <title><![CDATA[Older &amp; <b>bold</b>   episode]]></title>
    <guid isPermaLink="false">guid-old</guid>
    <pubDate>Mon, 01 Sep 2025 10:00:00 +0000</pubDate>
    <itunes:duration>01:02:03</itunes:duration>
    <enclosure url="https://cdn.example.org/old.mp3" type="audio/mpeg" length="1234"/>
  </item>
  <item>
    <title>Newest episode</title>
    <guid>guid-new</guid>
    <pubDate>Wed, 03 Sep 2025 06:30:00 -0400</pubDate>
    <itunes:duration>12:34</itunes:duration>
    <enclosure url="https://cdn.example.org/new.mp3" type="audio/mpeg" length="0"/>
  </item>
  <item>
    <title>Middle, no guid</title>
    <pubDate>Tue, 02 Sep 2025 10:00:00 GMT</pubDate>
    <itunes:duration>754</itunes:duration>
    <enclosure url="https://cdn.example.org/mid.mp3" type="audio/mpeg" length="99"/>
  </item>
  <item>
    <title>No date, skipped</title>
    <guid>guid-nodate</guid>
    <pubDate>yesterday</pubDate>
    <enclosure url="https://cdn.example.org/x.mp3" type="audio/mpeg"/>
  </item>
  <item>
    <title>No enclosure, skipped</title>
    <guid>guid-noenc</guid>
    <pubDate>Mon, 01 Sep 2025 10:00:00 +0000</pubDate>
  </item>
</channel>
</rss>
"""

ATOM = """<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Atom Show</title>
  <entry>
    <id>urn:uuid:one</id>
    <title type="html">First &lt;i&gt;atom&lt;/i&gt;</title>
    <updated>2025-09-01T10:00:00Z</updated>
    <link rel="alternate" href="https://example.org/one"/>
    <link rel="enclosure" href="https://cdn.example.org/one.m4a" type="audio/mp4" length="555"/>
  </entry>
  <entry>
    <id>urn:uuid:two</id>
    <title>Second atom</title>
    <published>2025-09-02T10:00:00+02:00</published>
    <link rel="enclosure" href="https://cdn.example.org/two.m4a" type="audio/mp4"/>
  </entry>
  <entry>
    <id>urn:uuid:three</id>
    <title>No enclosure</title>
    <updated>2025-09-03T10:00:00Z</updated>
  </entry>
</feed>
"""

CHANNELS_MD = """# Channels

Some prose with | pipes | in it.

| # | Channel | Feed |
|---|---------|------|
| 2 | Up First | https://feeds.npr.org/510318/podcast.xml |
| 1 | NPR News Now | https://feeds.npr.org/500005/podcast.xml |
| 3 |  | https://blank.example.org/rss |
| 4 | Not https | ftp://example.org/rss |
| 21 | Out of range | https://example.org/21 |
| 5 | The Daily | https://feeds.simplecast.com/54nAGcIl |
"""


class FakeFetcher:
    """Serves canned documents and redirect targets; records calls."""

    def __init__(
        self,
        docs: dict[str, str] | None = None,
        resolves: dict[str, str] | None = None,
    ) -> None:
        self.docs = docs or {}
        self.resolves = resolves or {}
        self.get_calls: list[str] = []
        self.resolve_calls: list[str] = []
        self.closed = False

    async def get_text(self, url: str) -> str:
        from jukebox_resolver.fetcher import FetchError

        self.get_calls.append(url)
        if url not in self.docs:
            raise FetchError(f"GET {url}: 404")
        return self.docs[url]

    async def resolve(self, url: str) -> str:
        self.resolve_calls.append(url)
        return self.resolves.get(url, url)

    async def aclose(self) -> None:
        self.closed = True


@pytest.fixture
def fake_fetcher() -> FakeFetcher:
    return FakeFetcher(
        docs={CHANNELS_URL: CHANNELS_MD, FEED_URL: RSS},
        resolves={"https://cdn.example.org/new.mp3": "https://final.example.org/new.mp3"},
    )
