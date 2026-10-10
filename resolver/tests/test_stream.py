"""GET /stream: the device plays audio through the resolver over plain HTTP."""

from __future__ import annotations

import asyncio

import httpx
import pytest
from fastapi.testclient import TestClient

from conftest import CHANNELS_MD, CHANNELS_URL, FEED_URL, RSS
from jukebox_resolver import stream as stream_module
from jukebox_resolver.app import create_app
from jukebox_resolver.config import STREAM_CONCURRENCY, Settings
from jukebox_resolver.fetcher import Fetcher

AUDIO = bytes(range(256)) * 64  # 16 KiB of recognisable bytes
NEW = "https://cdn.example.org/new.mp3"
OLD = "https://cdn.example.org/old.mp3"
PUBLIC = "93.184.216.34"


class Upstream:
    """A scriptable origin: channel list, feed, and audio with ranges and redirects."""

    def __init__(self) -> None:
        self.audio: dict[str, httpx.Response | bytes] = {NEW: AUDIO, OLD: AUDIO}
        self.gets: list[tuple[str, str | None]] = []

    def __call__(self, request: httpx.Request) -> httpx.Response:
        url = str(request.url.copy_with(host=request.headers["host"]))
        if url == CHANNELS_URL:
            return httpx.Response(200, stream=httpx.ByteStream(CHANNELS_MD.encode()))
        if url == FEED_URL:
            return httpx.Response(200, stream=httpx.ByteStream(RSS.encode()))
        if request.method == "HEAD":
            return httpx.Response(200)
        self.gets.append((url, request.headers.get("range")))
        answer = self.audio.get(url)
        if answer is None:
            return httpx.Response(404)
        if isinstance(answer, httpx.Response):
            return answer
        return _ranged(answer, request.headers.get("range"))


def _ranged(body: bytes, range_header: str | None) -> httpx.Response:
    headers = {"content-type": "audio/mpeg", "accept-ranges": "bytes"}
    start = int(range_header.removeprefix("bytes=").split("-")[0]) if range_header else 0
    part = body[start:]
    headers["content-length"] = str(len(part))  # real origins declare it; ByteStream does not
    if not range_header:
        return httpx.Response(200, headers=headers, stream=httpx.ByteStream(part))
    headers["content-range"] = f"bytes {start}-{len(body) - 1}/{len(body)}"
    return httpx.Response(206, headers=headers, stream=httpx.ByteStream(part))


def redirect(to: str) -> httpx.Response:
    return httpx.Response(302, headers={"location": to})


@pytest.fixture
def upstream() -> Upstream:
    return Upstream()


@pytest.fixture
def client(tmp_path, upstream):
    hosts = {"internal.example.org": ["10.0.0.5"]}
    fetcher = Fetcher(
        transport=httpx.MockTransport(upstream), resolver=lambda h: hosts.get(h, [PUBLIC])
    )
    settings = Settings(db_path=str(tmp_path / "s.db"), channels_url=CHANNELS_URL)
    with TestClient(create_app(settings, fetcher=fetcher)) as c:
        assert c.get("/episodes", params={"feed": FEED_URL}).status_code == 200  # fill the cache
        yield c


def free_slots(client) -> int:
    return client.app.state.stream_slots._value


def test_relays_the_whole_file(client, upstream):
    resp = client.get("/stream", params={"url": NEW})
    assert resp.status_code == 200
    assert resp.content == AUDIO
    assert resp.headers["content-type"] == "audio/mpeg"
    assert resp.headers["content-length"] == str(len(AUDIO))
    assert resp.headers["accept-ranges"] == "bytes"
    assert upstream.gets == [(NEW, None)]
    assert free_slots(client) == STREAM_CONCURRENCY


def test_passes_a_resume_range_through(client, upstream):
    resp = client.get("/stream", params={"url": NEW}, headers={"Range": "bytes=4096-"})
    assert resp.status_code == 206
    assert resp.content == AUDIO[4096:]
    assert resp.headers["content-range"] == f"bytes 4096-{len(AUDIO) - 1}/{len(AUDIO)}"
    assert upstream.gets == [(NEW, "bytes=4096-")]


def test_follows_redirects_and_keeps_the_range(client, upstream):
    signed = "https://media.example.org/signed/new.mp3?sig=abc"
    upstream.audio[NEW] = redirect(signed)
    upstream.audio[signed] = AUDIO
    resp = client.get("/stream", params={"url": NEW}, headers={"Range": "bytes=10-"})
    assert resp.status_code == 206 and resp.content == AUDIO[10:]
    assert upstream.gets == [(NEW, "bytes=10-"), (signed, "bytes=10-")]


def test_unknown_urls_are_not_fetched(client, upstream):
    resp = client.get("/stream", params={"url": "https://cdn.example.org/not-an-episode.mp3"})
    assert resp.status_code == 404
    assert resp.json() == {"ok": False, "error": "url is not a known episode"}
    assert client.get("/stream").status_code == 400
    assert client.get("/stream", params={"url": "http://169.254.169.254/x"}).status_code == 404
    assert upstream.gets == []


@pytest.mark.parametrize("bad", ["bytes=-500", "bytes=5-2,9-", "items=0-", "bytes=a-", "bytes=9-3"])
def test_rejects_ranges_it_does_not_understand(client, upstream, bad):
    resp = client.get("/stream", params={"url": NEW}, headers={"Range": bad})
    assert resp.status_code == 416
    assert upstream.gets == []
    assert free_slots(client) == STREAM_CONCURRENCY


def test_upstream_failure_is_502_and_frees_the_slot(client, upstream):
    upstream.audio[OLD] = httpx.Response(503)
    resp = client.get("/stream", params={"url": OLD})
    assert resp.status_code == 502 and resp.json()["ok"] is False
    assert free_slots(client) == STREAM_CONCURRENCY


def test_redirect_into_a_private_network_is_refused(client, upstream):
    upstream.audio[NEW] = redirect("http://internal.example.org/secret")
    resp = client.get("/stream", params={"url": NEW})
    assert resp.status_code == 502
    assert "refused" in resp.json()["error"]
    assert free_slots(client) == STREAM_CONCURRENCY


def test_refuses_files_past_the_size_cap(client, upstream, monkeypatch):
    monkeypatch.setattr(stream_module, "STREAM_MAX_BYTES", len(AUDIO) - 1)
    resp = client.get("/stream", params={"url": NEW})
    assert resp.status_code == 502
    assert free_slots(client) == STREAM_CONCURRENCY


def test_stops_relaying_at_the_cap_when_no_length_is_declared(client, upstream, monkeypatch):
    async def chunks():
        for _ in range(8):
            yield b"x" * 1000

    upstream.audio[NEW] = httpx.Response(200, content=chunks())
    monkeypatch.setattr(stream_module, "STREAM_MAX_BYTES", 2500)
    monkeypatch.setattr(stream_module, "STREAM_CHUNK_BYTES", 1000)
    resp = client.get("/stream", params={"url": NEW})
    assert resp.status_code == 200
    assert len(resp.content) == 2000  # whole chunks only, never past the cap
    assert free_slots(client) == STREAM_CONCURRENCY


def test_says_429_when_every_slot_is_busy(client, upstream):
    client.app.state.stream_slots = asyncio.Semaphore(0)
    resp = client.get("/stream", params={"url": NEW})
    assert resp.status_code == 429
    assert upstream.gets == []


def test_non_audio_content_type_is_not_passed_on(client, upstream):
    upstream.audio[NEW] = httpx.Response(
        200, headers={"content-type": "text/html"}, stream=httpx.ByteStream(b"<html>")
    )
    resp = client.get("/stream", params={"url": NEW})
    assert resp.headers["content-type"] == "audio/mpeg"


def test_falls_back_to_the_enclosure_when_the_resolved_url_has_expired(tmp_path):
    expired = "https://media.example.org/signed/old-sig.mp3"
    fresh = "https://media.example.org/signed/new-sig.mp3"
    origin = Upstream()
    state = {"resolved": False}

    def handler(request: httpx.Request) -> httpx.Response:
        url = str(request.url.copy_with(host=request.headers["host"]))
        if request.method == "HEAD" and url == NEW:  # /episodes resolution sees the old link
            state["resolved"] = True
            return redirect(expired)
        return origin(request)

    origin.audio[NEW] = redirect(fresh)
    origin.audio[expired] = httpx.Response(403)
    origin.audio[fresh] = AUDIO
    fetcher = Fetcher(transport=httpx.MockTransport(handler), resolver=lambda _h: [PUBLIC])
    settings = Settings(db_path=str(tmp_path / "f.db"), channels_url=CHANNELS_URL)
    with TestClient(create_app(settings, fetcher=fetcher)) as c:
        listed = c.get("/episodes", params={"feed": FEED_URL}).json()["data"]
        assert state["resolved"] and listed[0]["url"] == expired
        resp = c.get("/stream", params={"url": expired})  # the device sends what it was given
        assert resp.status_code == 200 and resp.content == AUDIO
        assert [u for u, _ in origin.gets] == [expired, NEW, fresh]
