"""SQLite persistence: listening positions, feed cache, last-good channel list."""

from __future__ import annotations

import json
import sqlite3
import threading
import time
from datetime import UTC, datetime
from typing import Any

SCHEMA = """
CREATE TABLE IF NOT EXISTS positions (
    device     TEXT NOT NULL,
    episode    TEXT NOT NULL,
    pos_s      INTEGER NOT NULL,
    duration_s INTEGER,
    updated    TEXT NOT NULL,
    PRIMARY KEY (device, episode)
);
CREATE TABLE IF NOT EXISTS feed_cache (
    feed       TEXT PRIMARY KEY,
    fetched_at REAL NOT NULL,
    episodes   TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS kv (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at REAL NOT NULL
);
"""


def now_iso() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


class Store:
    """Thread-safe wrapper around one sqlite3 connection."""

    def __init__(self, path: str) -> None:
        self._conn = sqlite3.connect(path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._lock = threading.Lock()
        with self._lock:
            self._conn.execute("PRAGMA journal_mode=WAL")
            self._conn.executescript(SCHEMA)

    def close(self) -> None:
        with self._lock:
            self._conn.close()

    # -- positions -----------------------------------------------------------

    def get_position(self, device: str, episode: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT pos_s, duration_s, updated FROM positions WHERE device=? AND episode=?",
                (device, episode),
            ).fetchone()
        return dict(row) if row else None

    def upsert_position(
        self, device: str, episode: str, pos_s: int, duration_s: int | None
    ) -> dict[str, Any]:
        updated = now_iso()
        with self._lock, self._conn:
            self._conn.execute(
                """
                INSERT INTO positions (device, episode, pos_s, duration_s, updated)
                VALUES (?, ?, ?, ?, ?)
                ON CONFLICT(device, episode) DO UPDATE SET
                    pos_s = excluded.pos_s,
                    duration_s = COALESCE(excluded.duration_s, positions.duration_s),
                    updated = excluded.updated
                """,
                (device, episode, pos_s, duration_s, updated),
            )
        row = self.get_position(device, episode)
        assert row is not None
        return row

    def list_positions(self, device: str) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT episode, pos_s, duration_s, updated FROM positions "
                "WHERE device=? ORDER BY updated DESC",
                (device,),
            ).fetchall()
        return [dict(r) for r in rows]

    # -- feed cache ----------------------------------------------------------

    def get_feed(self, feed: str) -> tuple[float, list[dict[str, Any]]] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT fetched_at, episodes FROM feed_cache WHERE feed=?", (feed,)
            ).fetchone()
        if row is None:
            return None
        return float(row["fetched_at"]), json.loads(row["episodes"])

    def put_feed(
        self, feed: str, episodes: list[dict[str, Any]], fetched_at: float | None = None
    ) -> float:
        fetched_at = time.time() if fetched_at is None else fetched_at
        with self._lock, self._conn:
            self._conn.execute(
                "INSERT INTO feed_cache (feed, fetched_at, episodes) VALUES (?, ?, ?) "
                "ON CONFLICT(feed) DO UPDATE SET fetched_at=excluded.fetched_at, "
                "episodes=excluded.episodes",
                (feed, fetched_at, json.dumps(episodes)),
            )
        return fetched_at

    # -- key/value (last-good channel list) ----------------------------------

    def get_kv(self, key: str) -> tuple[float, str] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT value, updated_at FROM kv WHERE key=?", (key,)
            ).fetchone()
        return (float(row["updated_at"]), row["value"]) if row else None

    def put_kv(self, key: str, value: str) -> None:
        with self._lock, self._conn:
            self._conn.execute(
                "INSERT INTO kv (key, value, updated_at) VALUES (?, ?, ?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value, "
                "updated_at=excluded.updated_at",
                (key, value, time.time()),
            )
