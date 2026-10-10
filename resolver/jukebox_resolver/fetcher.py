"""Outbound HTTP: manual redirect following, capped bodies, bounded probes and budgets."""

from __future__ import annotations

import asyncio
import logging
import zlib
from urllib.parse import urljoin

import httpx

from . import __version__
from .config import (
    GET_BUDGET_S,
    HTTP_TIMEOUT_S,
    MAX_REDIRECTS,
    PROBE_CONCURRENCY,
    RESOLVE_BUDGET_S,
    RESOLVE_TIMEOUT_S,
)
from .guard import (  # noqa: F401 - re-exported for callers and tests
    BlockedUrlError,
    FetchError,
    GuardedTransport,
    Resolver,
    check_url,
    is_http_url,
    is_public_ip,
    system_resolver,
)

log = logging.getLogger("jukebox.fetcher")
USER_AGENT = f"podcast-jukebox-resolver/{__version__}"
REDIRECT_CODES = (301, 302, 303, 307, 308)


def _location(resp: httpx.Response, current: str) -> str | None:
    target = resp.headers.get("location")
    if resp.status_code in REDIRECT_CODES and target:
        return urljoin(current, target)
    return None


class Fetcher:
    """One httpx client behind the guard. Never auto-follows; every hop is re-checked."""

    def __init__(
        self,
        transport: httpx.AsyncBaseTransport | None = None,
        resolver: Resolver | None = None,
    ) -> None:
        inner = transport or httpx.AsyncHTTPTransport(
            retries=0, limits=httpx.Limits(max_connections=16, max_keepalive_connections=8)
        )
        self._client = httpx.AsyncClient(
            headers={"User-Agent": USER_AGENT, "Accept-Encoding": "identity"},
            timeout=httpx.Timeout(HTTP_TIMEOUT_S),
            follow_redirects=False,
            transport=GuardedTransport(inner, resolver or system_resolver),
        )
        self._probes = asyncio.Semaphore(PROBE_CONCURRENCY)

    async def aclose(self) -> None:
        await self._client.aclose()

    async def get_text(self, url: str, max_bytes: int) -> str:
        """GET a text document within GET_BUDGET_S, at most MAX_REDIRECTS hops, body capped."""
        try:
            async with asyncio.timeout(GET_BUDGET_S):
                return await self._get_text(url, max_bytes)
        except TimeoutError as exc:
            raise FetchError(f"GET {url}: exceeded {GET_BUDGET_S} s budget") from exc

    async def _get_text(self, url: str, max_bytes: int) -> str:
        current = url
        for hop in range(MAX_REDIRECTS + 1):
            try:
                async with self._client.stream("GET", current) as resp:
                    target = _location(resp, current)
                    if target is None:
                        resp.raise_for_status()
                        return await _read_capped(resp, max_bytes)
            except BlockedUrlError as exc:
                if hop == 0:
                    raise
                raise FetchError(f"redirect refused: {exc}") from exc
            except httpx.HTTPError as exc:
                raise FetchError(f"GET {current}: {exc.__class__.__name__}: {exc}") from exc
            current = target
        raise FetchError(f"GET {url}: more than {MAX_REDIRECTS} redirects")

    async def resolve(self, url: str) -> str:
        """Final URL after redirects. HEAD first, then a GET probe, else the input URL.

        Bounded by PROBE_CONCURRENCY in-flight probes and RESOLVE_BUDGET_S per call.
        """
        check_url(url)
        try:
            async with self._probes, asyncio.timeout(RESOLVE_BUDGET_S):
                final = await self._probe("HEAD", url)
                if final is None:
                    final = await self._probe("GET", url)
        except TimeoutError:
            log.info("resolve %s exceeded %s s budget", url, RESOLVE_BUDGET_S)
            return url
        return final or url

    async def open_stream(self, url: str, range_header: str | None = None) -> httpx.Response:
        """Open `url` for relaying, following redirects by hand; the caller must aclose().

        Only a 200 or 206 comes back. The Range header rides along on every hop.
        """
        headers = {"Range": range_header} if range_header else {}
        current = url
        for hop in range(MAX_REDIRECTS + 1):
            try:
                request = self._client.build_request("GET", current, headers=headers)
                resp = await self._client.send(request, stream=True)
            except BlockedUrlError as exc:
                if hop == 0:
                    raise
                raise FetchError(f"redirect refused: {exc}") from exc
            except httpx.HTTPError as exc:
                raise FetchError(f"GET {current}: {exc.__class__.__name__}: {exc}") from exc
            target = _location(resp, current)
            if target is None and resp.status_code in (200, 206):
                return resp
            await resp.aclose()
            if target is None:
                raise FetchError(f"GET {current}: HTTP {resp.status_code}")
            current = target
        raise FetchError(f"GET {url}: more than {MAX_REDIRECTS} redirects")

    async def _probe(self, method: str, url: str) -> str | None:
        current = url
        for hop in range(MAX_REDIRECTS + 1):
            try:
                async with self._client.stream(method, current, timeout=RESOLVE_TIMEOUT_S) as resp:
                    target = _location(resp, current)
                    if target is None:
                        return current if resp.status_code < 400 else None
            except BlockedUrlError as exc:
                if hop == 0:
                    raise
                log.info("resolve %s %s refused hop: %s", method, current, exc)
                return None
            except httpx.HTTPError as exc:
                log.info("resolve %s %s stopped: %s", method, current, exc.__class__.__name__)
                return None
            current = target
        return None


def _decoder(encoding: str) -> zlib._Decompress | None:
    if encoding in ("", "identity"):
        return None
    if encoding in ("gzip", "x-gzip"):
        return zlib.decompressobj(16 + zlib.MAX_WBITS)
    if encoding == "deflate":
        return zlib.decompressobj()
    raise FetchError(f"unsupported content-encoding {encoding!r}")


async def _read_capped(resp: httpx.Response, max_bytes: int) -> str:
    """Read the raw body, refusing before any byte past max_bytes is buffered or decoded."""
    declared = resp.headers.get("content-length", "")
    if declared.isdigit() and int(declared) > max_bytes:
        raise FetchError(f"body of {declared} bytes exceeds the {max_bytes} byte cap")
    decoder = _decoder(resp.headers.get("content-encoding", "").strip().lower())
    out = bytearray()
    raw_total = 0
    async for raw in resp.aiter_raw():
        raw_total += len(raw)
        if raw_total > max_bytes:
            raise FetchError(f"body exceeds the {max_bytes} byte cap")
        data = decoder.decompress(raw, max_bytes - len(out) + 1) if decoder else raw
        if len(out) + len(data) > max_bytes or (decoder and decoder.unconsumed_tail):
            raise FetchError(f"decoded body exceeds the {max_bytes} byte cap")
        out += data
    return bytes(out).decode(resp.encoding or "utf-8", errors="replace")
