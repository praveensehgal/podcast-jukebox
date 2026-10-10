"""Configuration from environment variables."""

from __future__ import annotations

import os
from dataclasses import dataclass

DEFAULT_CHANNELS_URL = (
    "https://github.com/praveensehgal/podcast-jukebox/releases/download/channels/channels.md"
)

CHANNELS_TTL_S = 10 * 60
FEED_TTL_S = 15 * 60
HTTP_TIMEOUT_S = 15.0
RESOLVE_TIMEOUT_S = 8.0
MAX_REDIRECTS = 6
FEED_MAX_BYTES = 5 * 1024 * 1024
CHANNELS_MAX_BYTES = 256 * 1024
GET_BUDGET_S = 30.0  # whole GET including redirects
RESOLVE_BUDGET_S = 20.0  # whole HEAD+GET probe chain for one URL
FEED_REFRESH_BUDGET_S = 35.0
NEGATIVE_TTL_S = 60
PROBE_CONCURRENCY = 8  # in-flight redirect probes, service-wide
EPISODES_REQUEST_BUDGET_S = 20.0  # whole /episodes request incl. URL resolution
MAX_CONCURRENT_REFRESHES = 4  # upstream feed fetches in flight, service-wide
MAX_EPISODES = 50
DEFAULT_EPISODES = 20
TITLE_MAX_CHARS = 200


@dataclass(frozen=True)
class Settings:
    db_path: str = "./resolver.db"
    port: int = 8787
    channels_url: str = DEFAULT_CHANNELS_URL
    token: str | None = None

    @classmethod
    def from_env(cls, env: dict[str, str] | None = None) -> Settings:
        src = os.environ if env is None else env
        token = src.get("JUKEBOX_TOKEN", "").strip() or None
        return cls(
            db_path=src.get("JUKEBOX_DB", "./resolver.db"),
            port=int(src.get("JUKEBOX_PORT", "8787")),
            channels_url=src.get("JUKEBOX_CHANNELS_URL", DEFAULT_CHANNELS_URL),
            token=token,
        )
