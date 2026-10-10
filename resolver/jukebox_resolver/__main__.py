"""`python -m jukebox_resolver` / `jukebox-resolver`: run uvicorn with env-driven settings."""

from __future__ import annotations

import os

import uvicorn

from .config import Settings


def main() -> None:
    settings = Settings.from_env()
    uvicorn.run(
        "jukebox_resolver.server:app",
        host=os.environ.get("JUKEBOX_HOST", "127.0.0.1"),
        port=settings.port,
        access_log=False,
    )


if __name__ == "__main__":
    main()
