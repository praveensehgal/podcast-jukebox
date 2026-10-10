import httpx
import pytest

from jukebox_resolver.fetcher import (
    USER_AGENT,
    BlockedUrlError,
    Fetcher,
    FetchError,
    is_http_url,
    is_public_ip,
)

PUBLIC = "93.184.216.34"
HOSTS = {
    "cdn.test": [PUBLIC],
    "feeds.test": [PUBLIC, "2606:2800:220:1:248:1893:25c8:1946"],
    "internal.test": ["10.0.0.5"],
    "mixed.test": [PUBLIC, "192.168.1.9"],
    "localhost": ["127.0.0.1"],
}


def fake_resolver(host: str) -> list[str]:
    if host not in HOSTS:
        raise OSError("name not known")
    return HOSTS[host]


def make(handler, **kw) -> Fetcher:
    return Fetcher(transport=httpx.MockTransport(handler), resolver=fake_resolver, **kw)


def _transport(hops: int = 2, head_status: int = 200, get_status: int = 200):
    seen: list[tuple[str, str]] = []

    def handler(request: httpx.Request) -> httpx.Response:
        seen.append((request.method, str(request.url)))
        path = request.url.path
        if path.startswith("/hop"):
            n = int(path[4:])
            if n < hops:
                return httpx.Response(302, headers={"location": f"/hop{n + 1}"})
            status = head_status if request.method == "HEAD" else get_status
            return httpx.Response(status, content=b"audio")
        if path == "/feed.xml":
            return httpx.Response(200, text="<rss/>")
        if path == "/loop":
            return httpx.Response(302, headers={"location": "https://cdn.test/loop"})
        if path == "/ftp":
            return httpx.Response(302, headers={"location": "ftp://cdn.test/x"})
        if path == "/to-internal":
            return httpx.Response(302, headers={"location": "http://internal.test/secret"})
        if path == "/to-port":
            return httpx.Response(302, headers={"location": "https://cdn.test:8443/x"})
        if path == "/big":
            return httpx.Response(200, content=b"x" * (300 * 1024))
        if path == "/lies":
            return httpx.Response(200, headers={"content-length": "9999999"}, content=b"")
        return httpx.Response(404)

    return handler, seen


@pytest.mark.asyncio
async def test_resolve_follows_relative_redirects_with_head():
    handler, seen = _transport(hops=2)
    fetcher = make(handler)
    assert await fetcher.resolve("https://cdn.test/hop0") == "https://cdn.test/hop2"
    assert [m for m, _ in seen] == ["HEAD", "HEAD", "HEAD"]
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_resolve_falls_back_to_get_then_original():
    handler, seen = _transport(hops=1, head_status=405)
    fetcher = make(handler)
    assert await fetcher.resolve("https://cdn.test/hop0") == "https://cdn.test/hop1"
    assert [m for m, _ in seen][-1] == "GET"
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
    assert all("internal.test" not in url and ":8443" not in url for _, url in seen)
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
    for url in ("https://cdn.test:443/feed.xml", "http://cdn.test:80/feed.xml", "https://feeds.test/feed.xml"):
        assert await fetcher.get_text(url, 1024) == "<rss/>"
    assert await fetcher.resolve(f"http://{PUBLIC}/hop2") == f"http://{PUBLIC}/hop2"
    with pytest.raises(FetchError, match="cannot resolve"):
        await fetcher.get_text("https://unknown.test/feed.xml", 1024)
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_body_cap():
    handler, _ = _transport()
    fetcher = make(handler)
    assert len(await fetcher.get_text("https://cdn.test/big", 400 * 1024)) == 300 * 1024
    with pytest.raises(FetchError, match="exceeds"):
        await fetcher.get_text("https://cdn.test/big", 256 * 1024)
    with pytest.raises(FetchError, match="exceeds"):
        await fetcher.get_text("https://cdn.test/lies", 256 * 1024)
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_get_text_sets_user_agent_and_raises_on_error():
    handler, _ = _transport()
    captured: list[str] = []

    def wrapped(request: httpx.Request) -> httpx.Response:
        captured.append(request.headers["user-agent"])
        return handler(request)

    fetcher = make(wrapped)
    assert await fetcher.get_text("https://cdn.test/feed.xml", 1024) == "<rss/>"
    assert captured == [USER_AGENT]
    with pytest.raises(FetchError):
        await fetcher.get_text("https://cdn.test/missing", 1024)
    await fetcher.aclose()


@pytest.mark.parametrize(
    ("ip", "public"),
    [
        (PUBLIC, True),
        ("2606:4700::1111", True),
        ("127.0.0.1", False),
        ("10.0.0.1", False),
        ("172.31.255.255", False),
        ("192.168.0.1", False),
        ("169.254.169.254", False),
        ("100.127.0.1", False),
        ("224.0.0.1", False),
        ("::1", False),
        ("fe80::1%en0", False),
        ("fd00::1", False),
        ("::ffff:192.168.1.1", False),
        ("not-an-ip", False),
    ],
)
def test_is_public_ip(ip, public):
    assert is_public_ip(ip) is public


def test_is_http_url():
    assert is_http_url("https://a.b/c")
    assert is_http_url("http://a:80")
    assert not is_http_url("https://a:8443/")
    assert not is_http_url("ftp://a")
    assert not is_http_url("https://")
    assert not is_http_url("not a url")
