"""Outbound HTTP with an SSRF guard, manual redirect following and capped bodies."""

from __future__ import annotations

import asyncio
import ipaddress
import logging
import socket
from collections.abc import Callable
from urllib.parse import SplitResult, urljoin, urlsplit

import httpx

from . import __version__
from .config import HTTP_TIMEOUT_S, MAX_REDIRECTS, RESOLVE_TIMEOUT_S

log = logging.getLogger("jukebox.fetcher")
USER_AGENT = f"podcast-jukebox-resolver/{__version__}"
ALLOWED_PORTS = (80, 443)
REDIRECT_CODES = (301, 302, 303, 307, 308)
CGNAT = ipaddress.ip_network("100.64.0.0/10")

Resolver = Callable[[str], list[str]]


class FetchError(Exception):
    """An upstream download failed (network, status, size, redirect chain)."""


class BlockedUrlError(ValueError):
    """The URL is refused by policy: scheme, port, or a non-public host."""


def is_public_ip(text: str) -> bool:
    try:
        ip = ipaddress.ip_address(text.split("%", 1)[0])
    except ValueError:
        return False
    if isinstance(ip, ipaddress.IPv6Address) and ip.ipv4_mapped is not None:
        ip = ip.ipv4_mapped
    if isinstance(ip, ipaddress.IPv4Address) and ip in CGNAT:
        return False
    return not (
        ip.is_private
        or ip.is_loopback
        or ip.is_link_local
        or ip.is_multicast
        or ip.is_reserved
        or ip.is_unspecified
    )


def is_http_url(url: str) -> bool:
    try:
        check_url(url)
    except ValueError:
        return False
    return True


def check_url(url: str) -> SplitResult:
    """Static checks: http(s) scheme, a host, and port 80/443 only. Raises BlockedUrlError."""
    try:
        parts = urlsplit(url)
        port = parts.port
    except ValueError as exc:
        raise BlockedUrlError("malformed url") from exc
    if parts.scheme not in ("http", "https") or not parts.hostname:
        raise BlockedUrlError("url must be http(s) with a host")
    if port is not None and port not in ALLOWED_PORTS:
        raise BlockedUrlError("only ports 80 and 443 are allowed")
    return parts


def system_resolver(host: str) -> list[str]:
    infos = socket.getaddrinfo(host, None, type=socket.SOCK_STREAM)
    return sorted({info[4][0] for info in infos})


def _location(resp: httpx.Response, current: str) -> str | None:
    target = resp.headers.get("location")
    if resp.status_code in REDIRECT_CODES and target:
        return urljoin(current, target)
    return None


class Fetcher:
    """One httpx client. Never auto-follows; every hop is re-checked against the guard."""

    def __init__(
        self,
        transport: httpx.AsyncBaseTransport | None = None,
        resolver: Resolver | None = None,
    ) -> None:
        self._resolver = resolver or system_resolver
        self._client = httpx.AsyncClient(
            headers={"User-Agent": USER_AGENT},
            timeout=httpx.Timeout(HTTP_TIMEOUT_S),
            follow_redirects=False,
            transport=transport,
        )

    async def aclose(self) -> None:
        await self._client.aclose()

    async def guard(self, url: str) -> None:
        """Raise BlockedUrlError if the URL or any address its host resolves to is not public."""
        host = check_url(url).hostname or ""
        try:
            ipaddress.ip_address(host)
            addresses = [host]
        except ValueError:
            try:
                addresses = await asyncio.to_thread(self._resolver, host)
            except (OSError, ValueError) as exc:
                raise FetchError(f"cannot resolve {host}") from exc
        if not addresses or not all(is_public_ip(a) for a in addresses):
            raise BlockedUrlError(f"host {host} is not a public address")

    async def _hop(self, target: str) -> str:
        try:
            await self.guard(target)
        except BlockedUrlError as exc:
            raise FetchError(f"redirect refused: {exc}") from exc
        return target

    async def get_text(self, url: str, max_bytes: int) -> str:
        """GET a text document, following at most MAX_REDIRECTS hops, body capped."""
        await self.guard(url)
        current = url
        for _ in range(MAX_REDIRECTS + 1):
            try:
                async with self._client.stream("GET", current) as resp:
                    target = _location(resp, current)
                    if target is None:
                        resp.raise_for_status()
                        return await _read_capped(resp, max_bytes)
            except httpx.HTTPError as exc:
                raise FetchError(f"GET {current}: {exc.__class__.__name__}: {exc}") from exc
            current = await self._hop(target)
        raise FetchError(f"GET {url}: more than {MAX_REDIRECTS} redirects")

    async def resolve(self, url: str) -> str:
        """Final URL after redirects. HEAD first, then a GET probe, else the input URL."""
        await self.guard(url)
        final = await self._probe("HEAD", url)
        if final is None:
            final = await self._probe("GET", url)
        return final or url

    async def _probe(self, method: str, url: str) -> str | None:
        current = url
        for _ in range(MAX_REDIRECTS + 1):
            try:
                async with self._client.stream(method, current, timeout=RESOLVE_TIMEOUT_S) as resp:
                    target = _location(resp, current)
                    if target is None:
                        return current if resp.status_code < 400 else None
                current = await self._hop(target)
            except (httpx.HTTPError, FetchError) as exc:
                log.info("resolve %s %s stopped: %s", method, current, exc)
                return None
        return None


async def _read_capped(resp: httpx.Response, max_bytes: int) -> str:
    declared = resp.headers.get("content-length", "")
    if declared.isdigit() and int(declared) > max_bytes:
        raise FetchError(f"body of {declared} bytes exceeds the {max_bytes} byte cap")
    chunks: list[bytes] = []
    total = 0
    async for chunk in resp.aiter_bytes():
        total += len(chunk)
        if total > max_bytes:
            raise FetchError(f"body exceeds the {max_bytes} byte cap")
        chunks.append(chunk)
    return b"".join(chunks).decode(resp.encoding or "utf-8", errors="replace")
