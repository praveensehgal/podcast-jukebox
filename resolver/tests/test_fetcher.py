import asyncio
import gzip

import httpx
import pytest

from jukebox_resolver import fetcher as fetcher_module
from jukebox_resolver.fetcher import (
    USER_AGENT,
    BlockedUrlError,
    Fetcher,
    FetchError,
)

PUBLIC = "93.184.216.34"
PUBLIC6 = "2606:2800:220:1:248:1893:25c8:1946"
HOSTS = {
    "cdn.test": [PUBLIC],
    "feeds.test": [PUBLIC6, PUBLIC],
    "six.test": [PUBLIC6],
    "internal.test": ["10.0.0.5"],
    "mixed.test": [PUBLIC, "192.168.1.9"],
    "localhost": ["127.0.0.1"],
}


def fake_resolver(host: str) -> list[str]:
    if host not in HOSTS:
        raise OSError("name not known")
    return HOSTS[host]


def make(handler) -> Fetcher:
    return Fetcher(transport=httpx.MockTransport(handler), resolver=fake_resolver)


def resp(status: int, body: bytes = b"", headers: dict[str, str] | None = None) -> httpx.Response:
    """Like a real transport: the body arrives as an unread stream, never pre-buffered."""
    return httpx.Response(status, headers=headers, stream=httpx.ByteStream(body))


def _transport(hops: int = 2, head_status: int = 200, get_status: int = 200):
    """Handler records (method, Host header, path); the URL host is the pinned IP."""
    seen: list[tuple[str, str, str]] = []

    def handler(request: httpx.Request) -> httpx.Response:
        seen.append((request.method, request.headers["host"], request.url.path))
        path = request.url.path
        if path.startswith("/hop"):
            n = int(path[4:])
            if n < hops:
                return resp(302, headers={"location": f"/hop{n + 1}"})
            status = head_status if request.method == "HEAD" else get_status
            return resp(status, b"audio")
        if path == "/feed.xml":
            return resp(200, b"<rss/>")
        if path == "/loop":
            return resp(302, headers={"location": "https://cdn.test/loop"})
        if path == "/ftp":
            return resp(302, headers={"location": "ftp://cdn.test/x"})
        if path == "/to-internal":
            return resp(302, headers={"location": "http://internal.test/secret"})
        if path == "/to-port":
            return resp(302, headers={"location": "https://cdn.test:8443/x"})
        if path == "/big":
            return resp(200, b"x" * (300 * 1024))
        if path == "/lies":
            return resp(200, b"", {"content-length": "9999999"})
        if path == "/gz":
            body = gzip.compress(b"<rss>ok</rss>")
            return resp(200, body, {"content-encoding": "gzip"})
        if path == "/bomb":
            body = gzip.compress(b"\0" * (4 * 1024 * 1024))  # ~4 KB on the wire
            return resp(200, body, {"content-encoding": "gzip"})
        if path == "/br":
            return resp(200, b"x", {"content-encoding": "br"})
        return resp(404)

    return handler, seen


@pytest.mark.asyncio
async def test_connection_is_pinned_to_vetted_ip_with_host_and_sni():
    pinned: list[tuple[str, str, str | None]] = []

    def handler(request: httpx.Request) -> httpx.Response:
        pinned.append(
            (request.url.host, request.headers["host"], request.extensions.get("sni_hostname"))
        )
        return resp(200, b"<rss/>")

    fetcher = make(handler)
    await fetcher.get_text("https://cdn.test/feed.xml", 1024)
    await fetcher.get_text("http://cdn.test/feed.xml", 1024)
    await fetcher.get_text("https://feeds.test/feed.xml", 1024)  # dual stack -> IPv4
    await fetcher.get_text("https://six.test/feed.xml", 1024)  # IPv6 only
    await fetcher.get_text(f"https://{PUBLIC}/feed.xml", 1024)  # literal IP, no SNI
    assert pinned == [
        (PUBLIC, "cdn.test", "cdn.test"),
        (PUBLIC, "cdn.test", None),
        (PUBLIC, "feeds.test", "feeds.test"),
        (PUBLIC6, "six.test", "six.test"),
        (PUBLIC, PUBLIC, None),
    ]
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_resolve_follows_relative_redirects_with_head():
    handler, seen = _transport(hops=2)
    fetcher = make(handler)
    assert await fetcher.resolve("https://cdn.test/hop0") == "https://cdn.test/hop2"
    assert [m for m, _, _ in seen] == ["HEAD", "HEAD", "HEAD"]
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_resolve_falls_back_to_get_then_original():
    handler, seen = _transport(hops=1, head_status=405)
    fetcher = make(handler)
    assert await fetcher.resolve("https://cdn.test/hop0") == "https://cdn.test/hop1"
    assert seen[-1][0] == "GET"
    handler2, _ = _transport(hops=1, head_status=405, get_status=403)
    fetcher2 = make(handler2)
    assert await fetcher2.resolve("https://cdn.test/hop0") == "https://cdn.test/hop0"
    await fetcher.aclose()
    await fetcher2.aclose()


@pytest.mark.asyncio
async def test_resolve_stops_on_loops_bad_schemes_and_hop_limit():
    handler, seen = _transport(hops=10)
    fetcher = make(handler)
    assert await fetcher.resolve("https://cdn.test/loop") == "https://cdn.test/loop"
    assert await fetcher.resolve("https://cdn.test/ftp") == "https://cdn.test/ftp"
    seen.clear()
    assert await fetcher.resolve("https://cdn.test/hop0") == "https://cdn.test/hop0"
    assert len(seen) <= 2 * 7  # at most 6 hops + 1 for HEAD, same again for GET
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_redirect_to_private_host_or_port_is_never_fetched():
    handler, seen = _transport()
    fetcher = make(handler)
    assert await fetcher.resolve("https://cdn.test/to-internal") == "https://cdn.test/to-internal"
    assert await fetcher.resolve("https://cdn.test/to-port") == "https://cdn.test/to-port"
    with pytest.raises(FetchError, match="redirect refused"):
        await fetcher.get_text("https://cdn.test/to-internal", 1024)
    with pytest.raises(FetchError, match="redirect refused"):
        await fetcher.get_text("https://cdn.test/to-port", 1024)
    assert all(host == "cdn.test" for _, host, _ in seen)
    await fetcher.aclose()


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "url",
    [
        "http://127.0.0.1/x",
        "http://localhost/x",
        "http://10.1.2.3/x",
        "http://172.16.5.5/x",
        "http://192.168.1.1/x",
        "http://169.254.169.254/latest/meta-data/",
        "http://100.64.1.1/x",
        "http://0.0.0.0/x",
        "http://[::1]/x",
        "http://[fe80::1]/x",
        "http://[fc00::1]/x",
        "http://[fd12::1]/x",
        "http://[::ffff:10.0.0.1]/x",
        "http://internal.test/x",
        "http://mixed.test/x",
        "https://cdn.test:8443/x",
        "http://cdn.test:8080/x",
        "ftp://cdn.test/x",
        "file:///etc/passwd",
        "javascript:alert(1)",
        "https:///nohost",
    ],
)
async def test_guard_blocks(url):
    handler, seen = _transport()
    fetcher = make(handler)
    with pytest.raises(BlockedUrlError):
        await fetcher.resolve(url)
    with pytest.raises(BlockedUrlError):
        await fetcher.get_text(url, 1024)
    assert seen == []
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_guard_allows_public_hosts_and_standard_ports():
    handler, _ = _transport()
    fetcher = make(handler)
    for url in ("https://cdn.test:443/feed.xml", "http://cdn.test:80/feed.xml"):
        assert await fetcher.get_text(url, 1024) == "<rss/>"
    assert await fetcher.resolve(f"http://{PUBLIC}/hop2") == f"http://{PUBLIC}/hop2"
    with pytest.raises(FetchError, match="cannot resolve"):
        await fetcher.get_text("https://unknown.test/feed.xml", 1024)
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_body_cap_before_buffering_and_after_decoding():
    handler, _ = _transport()
    fetcher = make(handler)
    assert len(await fetcher.get_text("https://cdn.test/big", 400 * 1024)) == 300 * 1024
    with pytest.raises(FetchError, match="exceeds"):
        await fetcher.get_text("https://cdn.test/big", 256 * 1024)
    with pytest.raises(FetchError, match="exceeds"):
        await fetcher.get_text("https://cdn.test/lies", 256 * 1024)
    assert await fetcher.get_text("https://cdn.test/gz", 1024) == "<rss>ok</rss>"
    with pytest.raises(FetchError, match="exceeds"):
        await fetcher.get_text("https://cdn.test/bomb", 1024 * 1024)  # 4 KB wire, 4 MB decoded
    with pytest.raises(FetchError, match="content-encoding"):
        await fetcher.get_text("https://cdn.test/br", 1024)
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_get_text_headers_and_errors():
    handler, _ = _transport()
    captured: list[tuple[str, str]] = []

    def wrapped(request: httpx.Request) -> httpx.Response:
        captured.append((request.headers["user-agent"], request.headers["accept-encoding"]))
        return handler(request)

    fetcher = make(wrapped)
    assert await fetcher.get_text("https://cdn.test/feed.xml", 1024) == "<rss/>"
    assert captured == [(USER_AGENT, "identity")]
    with pytest.raises(FetchError):
        await fetcher.get_text("https://cdn.test/missing", 1024)
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_time_budgets(monkeypatch):
    monkeypatch.setattr(fetcher_module, "GET_BUDGET_S", 0.05)
    monkeypatch.setattr(fetcher_module, "RESOLVE_BUDGET_S", 0.05)

    async def slow(_: httpx.Request) -> httpx.Response:
        await asyncio.sleep(0.5)
        return resp(200, b"<rss/>")

    fetcher = make(slow)
    with pytest.raises(FetchError, match="budget"):
        await fetcher.get_text("https://cdn.test/feed.xml", 1024)
    assert await fetcher.resolve("https://cdn.test/a.mp3") == "https://cdn.test/a.mp3"
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_probe_concurrency_is_bounded_service_wide():
    in_flight = 0
    peak = 0

    async def handler(_: httpx.Request) -> httpx.Response:
        nonlocal in_flight, peak
        in_flight += 1
        peak = max(peak, in_flight)
        await asyncio.sleep(0.01)
        in_flight -= 1
        return resp(200)

    fetcher = make(handler)
    urls = [f"https://cdn.test/{i}.mp3" for i in range(30)]
    assert await asyncio.gather(*(fetcher.resolve(u) for u in urls)) == urls
    assert peak <= fetcher_module.PROBE_CONCURRENCY
    await fetcher.aclose()
