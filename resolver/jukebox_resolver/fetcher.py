"""Outbound HTTP: feed/channel downloads and redirect resolution."""

from __future__ import annotations

import logging
from urllib.parse import urlsplit

import httpx

from . import __version__
from .config import HTTP_TIMEOUT_S, MAX_REDIRECTS, RESOLVE_TIMEOUT_S

log = logging.getLogger("jukebox.fetcher")
USER_AGENT = f"podcast-jukebox-resolver/{__version__}"


class FetchError(Exception):
    """An upstream download failed."""


def is_http_url(url: str) -> bool:
    try:
        parts = urlsplit(url)
    except ValueError:
        return False
    return parts.scheme in ("http", "https") and bool(parts.netloc)


def check_url(url: str) -> str:
    if not is_http_url(url):
        raise ValueError("url must be http(s)")
    return url


class Fetcher:
    """One httpx client. Follows at most MAX_REDIRECTS hops, 15 s timeout."""

    def __init__(self, transport: httpx.AsyncBaseTransport | None = None) -> None:
        self._client = httpx.AsyncClient(
            headers={"User-Agent": USER_AGENT},
            timeout=HTTP_TIMEOUT_S,
            follow_redirects=True,
            max_redirects=MAX_REDIRECTS,
            transport=transport,
        )

    async def aclose(self) -> None:
        await self._client.aclose()

    async def get_text(self, url: str) -> str:
        """GET a text document (feed XML, channels markdown)."""
        check_url(url)
        try:
            resp = await self._client.get(url)
            resp.raise_for_status()
        except httpx.HTTPError as exc:
            raise FetchError(f"GET {url}: {exc.__class__.__name__}: {exc}") from exc
        return resp.text

    async def resolve(self, url: str) -> str:
        """Final URL after redirects. HEAD first, then a GET probe, else the input URL."""
        check_url(url)
        final = await self._probe("HEAD", url)
        if final is None:
            final = await self._probe("GET", url)
        if final is None or not is_http_url(final):
            return url
        return final

    async def _probe(self, method: str, url: str) -> str | None:
        try:
            async with self._client.stream(method, url, timeout=RESOLVE_TIMEOUT_S) as resp:
                if resp.status_code >= 400:
                    return None
                return str(resp.url)
        except httpx.HTTPError as exc:
            log.info("resolve %s %s failed: %s", method, url, exc.__class__.__name__)
            return None
