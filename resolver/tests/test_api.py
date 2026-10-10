import httpx
import pytest
from fastapi.testclient import TestClient

from conftest import CHANNELS_URL, FEED_URL, FakeFetcher
from jukebox_resolver.app import create_app
from jukebox_resolver.config import CHANNELS_MAX_BYTES, FEED_MAX_BYTES, Settings
from jukebox_resolver.fetcher import Fetcher


def make_client(tmp_path, fetcher, token=None):
    settings = Settings(db_path=str(tmp_path / "api.db"), channels_url=CHANNELS_URL, token=token)
    return TestClient(create_app(settings, fetcher=fetcher))


@pytest.fixture
def client(tmp_path, fake_fetcher):
    with make_client(tmp_path, fake_fetcher) as c:
        yield c


def test_health(client):
    body = client.get("/health").json()
    assert body["ok"] is True
    assert body["data"]["version"]
    assert body["data"]["channels"] == 0
    assert body["data"]["uptime_s"] >= 0


def test_channels_and_health_count(client, fake_fetcher):
    body = client.get("/channels").json()
    assert body["ok"] and [c["position"] for c in body["data"]] == [1, 2, 5]
    assert client.get("/health").json()["data"]["channels"] == 3
    client.get("/channels")
    assert fake_fetcher.get_calls.count(CHANNELS_URL) == 1


def test_episodes(client, fake_fetcher):
    body = client.get("/episodes", params={"feed": FEED_URL, "limit": 2}).json()
    assert body["ok"] and len(body["data"]) == 2
    first = body["data"][0]
    assert set(first) == {"id", "title", "published", "duration_s", "url", "mime", "size"}
    assert first["url"] == "https://final.example.org/new.mp3"  # resolved
    assert client.get("/episodes", params={"feed": FEED_URL}).json()["data"][1]["title"]
    assert fake_fetcher.get_calls.count(FEED_URL) == 1  # cached
    assert len(fake_fetcher.resolve_calls) == 3


def test_services_pass_body_caps(client, fake_fetcher):
    client.get("/channels")
    client.get("/episodes", params={"feed": FEED_URL})
    assert fake_fetcher.caps == [CHANNELS_MAX_BYTES, FEED_MAX_BYTES]


def test_private_urls_are_400(tmp_path):
    def handler(_: httpx.Request) -> httpx.Response:
        raise AssertionError("no request may reach the transport")

    real = Fetcher(transport=httpx.MockTransport(handler), resolver=lambda _h: ["10.0.0.9"])
    with make_client(tmp_path, real) as c:
        for url in ("http://169.254.169.254/latest", "http://intranet.local/x", "https://x.y:8443/"):
            resp = c.get("/resolve", params={"url": url})
            assert resp.status_code == 400, url
            assert resp.json()["error"].startswith("url refused"), resp.json()
        assert c.get("/episodes", params={"feed": "http://10.0.0.1/feed"}).status_code == 400


def test_episodes_validation(client):
    assert client.get("/episodes").status_code == 400
    bad = client.get("/episodes", params={"feed": "ftp://x/y"}).json()
    assert bad["ok"] is False and bad["error"].startswith("feed refused")
    assert client.get("/episodes", params={"feed": FEED_URL, "limit": "x"}).status_code == 400
    assert len(client.get("/episodes", params={"feed": FEED_URL, "limit": 999}).json()["data"]) == 3
    assert len(client.get("/episodes", params={"feed": FEED_URL, "limit": 0}).json()["data"]) == 1


def test_episodes_upstream_failure_is_502(client):
    body = client.get("/episodes", params={"feed": "https://example.org/missing.xml"})
    assert body.status_code == 502
    assert body.json()["ok"] is False


def test_position_roundtrip(client):
    empty = client.get("/position", params={"device": "esp-1", "episode": "abc"}).json()
    assert empty["data"] == {"pos_s": 0, "duration_s": None, "updated": None}
    posted = client.post(
        "/position", json={"device": "esp-1", "episode": "abc", "pos_s": 42, "duration_s": 600}
    ).json()
    assert posted["ok"] and posted["data"]["pos_s"] == 42
    got = client.get("/position", params={"device": "esp-1", "episode": "abc"}).json()["data"]
    assert got["pos_s"] == 42 and got["duration_s"] == 600 and got["updated"].endswith("Z")
    client.post("/position", json={"device": "esp-1", "episode": "def", "pos_s": 1})
    rows = client.get("/positions", params={"device": "esp-1"}).json()["data"]
    assert {r["episode"] for r in rows} == {"abc", "def"}


@pytest.mark.parametrize(
    "payload",
    [
        {"device": "bad id", "episode": "abc", "pos_s": 1},
        {"device": "ok", "episode": "x" * 65, "pos_s": 1},
        {"device": "ok", "episode": "abc", "pos_s": -1},
        {"device": "ok", "episode": "abc", "pos_s": "soon"},
        {"device": "ok", "episode": "abc"},
        {"device": "ok", "episode": "abc", "pos_s": 1, "duration_s": -3},
    ],
)
def test_position_validation(client, payload):
    resp = client.post("/position", json=payload)
    assert resp.status_code == 400
    assert resp.json()["ok"] is False and resp.json()["error"]


def test_position_query_validation(client):
    assert client.get("/position", params={"device": "a b", "episode": "x"}).status_code == 400
    assert client.get("/positions").status_code == 400


def test_resolve(client):
    body = client.get("/resolve", params={"url": "https://cdn.example.org/new.mp3"}).json()
    assert body["data"] == {"url": "https://final.example.org/new.mp3"}
    assert client.get("/resolve", params={"url": "javascript:alert(1)"}).status_code == 400


def test_unknown_route_uses_envelope(client):
    resp = client.get("/nope")
    assert resp.status_code == 404
    assert resp.json()["ok"] is False


def test_token_required_except_health(tmp_path, fake_fetcher):
    with make_client(tmp_path, fake_fetcher, token="s3cret") as c:
        assert c.get("/health").status_code == 200
        denied = c.get("/channels")
        assert denied.status_code == 401 and denied.json()["ok"] is False
        assert c.get("/channels", headers={"X-Jukebox-Token": "wrong"}).status_code == 401
        assert c.get("/channels", headers={"X-Jukebox-Token": "s3cret"}).status_code == 200


def test_lifespan_closes_fetcher(tmp_path):
    fetcher = FakeFetcher()
    with make_client(tmp_path, fetcher):
        pass
    assert fetcher.closed
