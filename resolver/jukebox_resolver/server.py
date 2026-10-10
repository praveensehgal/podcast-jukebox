"""ASGI entrypoint for uvicorn: `uvicorn jukebox_resolver.server:app`."""

from .app import create_app

app = create_app()
