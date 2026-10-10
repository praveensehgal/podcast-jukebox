import httpx
import pytest

from jukebox_resolver.fetcher import USER_AGENT, Fetcher, FetchError, is_http_url


def _transport(hops: int = 2, head_status: int = 200, get_status: int = 200):
    seen: list[tuple[str, str]] = []

    def handler(request: httpx.Request) -> httpx.Response:
        seen.append((request.method, str(request.url)))
        path = request.url.path
        if path.startswith("/hop"):
            n = int(path[4:])
            if n < hops:
                return httpx.Response(302, headers={"location": f"https://cdn.test/hop{n + 1}"})
            status = head_status if request.method == "HEAD" else get_status
            return httpx.Response(status, content=b"audio")
        if path == "/feed.xml":
            return httpx.Response(200, text="<rss/>")
        if path == "/loop":
            return httpx.Response(302, headers={"location": "https://cdn.test/loop"})
        if path == "/ftp":
            return httpx.Response(302, headers={"location": "ftp://cdn.test/x"})
        return httpx.Response(404)

    return httpx.MockTransport(handler), seen


@pytest.mark.asyncio
async def test_resolve_follows_redirects_with_head():
    transport, seen = _transport(hops=2)
    fetcher = Fetcher(transport=transport)
    assert await fetcher.resolve("https://cdn.test/hop0") == "https://cdn.test/hop2"
    assert all(m == "HEAD" for m, _ in seen)
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_resolve_falls_back_to_get_then_original():
    transport, seen = _transport(hops=1, head_status=405)
    fetcher = Fetcher(transport=transport)
    assert await fetcher.resolve("https://cdn.test/hop0") == "https://cdn.test/hop1"
    assert [m for m, _ in seen][-1] == "GET"
    transport, _ = _transport(hops=1, head_status=405, get_status=403)
    fetcher2 = Fetcher(transport=transport)
    assert await fetcher2.resolve("https://cdn.test/hop0") == "https://cdn.test/hop0"
    await fetcher.aclose()
    await fetcher2.aclose()


@pytest.mark.asyncio
async def test_resolve_gives_up_on_loops_and_bad_schemes():
    transport, _ = _transport()
    fetcher = Fetcher(transport=transport)
    assert await fetcher.resolve("https://cdn.test/loop") == "https://cdn.test/loop"
    assert await fetcher.resolve("https://cdn.test/ftp") == "https://cdn.test/ftp"
    with pytest.raises(ValueError):
        await fetcher.resolve("file:///etc/passwd")
    await fetcher.aclose()


@pytest.mark.asyncio
async def test_get_text_sets_user_agent_and_raises_on_error():
    transport, seen = _transport()
    captured: list[str] = []

    def handler(request: httpx.Request) -> httpx.Response:
        captured.append(request.headers["user-agent"])
        return transport.handler(request)

    fetcher = Fetcher(transport=httpx.MockTransport(handler))
    assert await fetcher.get_text("https://cdn.test/feed.xml") == "<rss/>"
    assert captured == [USER_AGENT]
    with pytest.raises(FetchError):
        await fetcher.get_text("https://cdn.test/missing")
    with pytest.raises(ValueError):
        await fetcher.get_text("gopher://cdn.test/")
    await fetcher.aclose()


def test_is_http_url():
    assert is_http_url("https://a.b/c")
    assert is_http_url("http://a")
    assert not is_http_url("ftp://a")
    assert not is_http_url("https://")
    assert not is_http_url("not a url")
