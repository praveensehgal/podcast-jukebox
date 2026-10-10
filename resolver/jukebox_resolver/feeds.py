"""RSS 2.0 / Atom parsing into flat episode dicts. No network here."""

from __future__ import annotations

import hashlib
import html
import re
from datetime import UTC, datetime
from email.utils import parsedate_to_datetime
from typing import Any
from xml.etree.ElementTree import Element  # noqa: S405 - parsed via defusedxml

from defusedxml import ElementTree as SafeET

from .config import MAX_EPISODES, TITLE_MAX_CHARS
from .guard import is_http_url

TAG_RE = re.compile(r"<[^>]+>")


class FeedError(Exception):
    """The document is not a usable RSS or Atom feed."""


def localname(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def child(el: Element, name: str) -> Element | None:
    for node in el:
        if localname(node.tag) == name:
            return node
    return None


def child_text(el: Element, name: str) -> str | None:
    node = child(el, name)
    if node is None or node.text is None:
        return None
    text = node.text.strip()
    return text or None


def clean_title(raw: str | None) -> str:
    if not raw:
        return "Untitled"
    text = TAG_RE.sub(" ", html.unescape(raw))
    text = " ".join(text.split())
    return text[:TITLE_MAX_CHARS] or "Untitled"


def parse_duration(raw: str | None) -> int | None:
    """'HH:MM:SS', 'MM:SS' or plain seconds -> int seconds; anything else -> None."""
    if raw is None:
        return None
    parts = raw.strip().split(":")
    if not raw.strip() or len(parts) > 3:
        return None
    try:
        numbers = [float(p) for p in parts]
    except ValueError:
        return None
    total = 0.0
    for number in numbers:
        total = total * 60 + number
    return int(total) if total >= 0 else None


def parse_date(raw: str | None) -> datetime | None:
    """RFC 2822 or ISO 8601 -> aware UTC datetime; unparseable -> None."""
    if not raw or not raw.strip():
        return None
    text = raw.strip()
    parsed: datetime | None = None
    try:
        parsed = parsedate_to_datetime(text)
    except (TypeError, ValueError, IndexError):
        try:
            parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
        except ValueError:
            return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)


def to_int(raw: str | None) -> int | None:
    if raw is None or not raw.strip().isdigit():
        return None
    value = int(raw.strip())
    return value if value > 0 else None


def episode_id(feed_url: str, key: str) -> str:
    return hashlib.sha1((feed_url + "|" + key).encode("utf-8")).hexdigest()[:16]


def _rss_item(feed_url: str, item: Element) -> tuple[datetime, dict[str, Any]] | None:
    enclosure = child(item, "enclosure")
    url = enclosure.get("url", "").strip() if enclosure is not None else ""
    published = parse_date(child_text(item, "pubDate"))
    if not is_http_url(url) or published is None:
        return None
    key = child_text(item, "guid") or url
    return published, {
        "id": episode_id(feed_url, key),
        "title": clean_title(child_text(item, "title")),
        "published": published.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "duration_s": parse_duration(child_text(item, "duration")),
        "url": url,
        "mime": enclosure.get("type") or None,
        "size": to_int(enclosure.get("length")),
    }


def _atom_enclosure(entry: Element) -> Element | None:
    for node in entry:
        if localname(node.tag) == "link" and node.get("rel") == "enclosure" and node.get("href"):
            return node
    return None


def _atom_entry(feed_url: str, entry: Element) -> tuple[datetime, dict[str, Any]] | None:
    link = _atom_enclosure(entry)
    published = parse_date(child_text(entry, "published") or child_text(entry, "updated"))
    if link is None or published is None:
        return None
    url = link.get("href", "").strip()
    if not is_http_url(url):
        return None
    key = child_text(entry, "id") or url
    return published, {
        "id": episode_id(feed_url, key),
        "title": clean_title(child_text(entry, "title")),
        "published": published.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "duration_s": parse_duration(child_text(entry, "duration")),
        "url": url,
        "mime": link.get("type") or None,
        "size": to_int(link.get("length")),
    }


def _collect(feed_url: str, root: Element) -> list[tuple[datetime, dict[str, Any]]]:
    kind = localname(root.tag)
    if kind == "rss":
        channel = child(root, "channel")
        items = [n for n in channel] if channel is not None else []
        rows = (_rss_item(feed_url, n) for n in items if localname(n.tag) == "item")
    elif kind == "feed":
        rows = (_atom_entry(feed_url, n) for n in root if localname(n.tag) == "entry")
    else:
        raise FeedError(f"unsupported root element <{kind}>")
    return [row for row in rows if row is not None]


def parse_feed(feed_url: str, xml_text: str) -> list[dict[str, Any]]:
    """Newest-first episodes (at most MAX_EPISODES) from an RSS 2.0 or Atom document."""
    try:
        root = SafeET.fromstring(xml_text)
    except Exception as exc:  # defusedxml raises several ParseError flavours
        raise FeedError(f"not well-formed XML: {exc}") from exc
    rows = _collect(feed_url, root)
    rows.sort(key=lambda row: row[0], reverse=True)
    return [episode for _, episode in rows[:MAX_EPISODES]]
