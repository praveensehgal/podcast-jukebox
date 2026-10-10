"""SSRF guard: URL policy, public-address check, and an httpx transport that pins
every connection to the address that passed the check (DNS rebinding safe)."""

from __future__ import annotations

import asyncio
import ipaddress
import socket
from collections.abc import Callable
from urllib.parse import SplitResult, urlsplit

import httpx

ALLOWED_PORTS = (80, 443)
CGNAT = ipaddress.ip_network("100.64.0.0/10")

Resolver = Callable[[str], list[str]]


class FetchError(Exception):
    """An upstream download failed (network, status, size, redirect chain, budget)."""


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


def _is_ip(text: str) -> bool:
    try:
        ipaddress.ip_address(text)
    except ValueError:
        return False
    return True


async def pin_address(host: str, resolver: Resolver) -> str:
    """Resolve once, require every address to be public, return the one to connect to."""
    if _is_ip(host):
        addresses = [host]
    else:
        try:
            addresses = await asyncio.to_thread(resolver, host)
        except (OSError, ValueError) as exc:
            raise FetchError(f"cannot resolve {host}") from exc
    if not addresses or not all(is_public_ip(a) for a in addresses):
        raise BlockedUrlError(f"host {host} is not a public address")
    v4 = [a for a in addresses if ":" not in a]
    return (v4 or addresses)[0]


class GuardedTransport(httpx.AsyncBaseTransport):
    """Checks every request (initial and each redirect hop) and connects to the vetted IP.

    The URL host is rewritten to the literal address; the Host header and TLS SNI keep
    the original hostname so certificates still verify. A second DNS answer can never
    change where the socket goes.
    """

    def __init__(self, inner: httpx.AsyncBaseTransport, resolver: Resolver) -> None:
        self._inner = inner
        self._resolver = resolver

    async def handle_async_request(self, request: httpx.Request) -> httpx.Response:
        parts = check_url(str(request.url))
        host = parts.hostname or ""
        address = await pin_address(host, self._resolver)
        extensions = dict(request.extensions)
        if parts.scheme == "https" and not _is_ip(host):
            extensions["sni_hostname"] = host
        pinned = httpx.Request(
            request.method,
            request.url.copy_with(host=address),
            headers=request.headers,
            stream=request.stream,
            extensions=extensions,
        )
        return await self._inner.handle_async_request(pinned)

    async def aclose(self) -> None:
        await self._inner.aclose()
