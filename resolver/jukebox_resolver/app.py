"""Application factory: wiring, request logging, token check."""

from __future__ import annotations

import hmac
import logging
import sys
import time
from collections.abc import AsyncIterator, Awaitable, Callable
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request, Response

from . import __version__
from .channels import ChannelService
from .config import Settings
from .episodes import EpisodeService
from .errors import err, install_error_handlers
from .fetcher import Fetcher
from .routes import router
from .store import Store

TOKEN_HEADER = "x-jukebox-token"
log = logging.getLogger("jukebox.app")


def configure_logging() -> None:
    root = logging.getLogger("jukebox")
    if root.handlers:
        return
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s %(message)s"))
    root.addHandler(handler)
    root.setLevel(logging.INFO)
    root.propagate = False


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncIterator[None]:
    log.info("resolver %s starting; db=%s", __version__, app.state.settings.db_path)
    yield
    await app.state.fetcher.aclose()
    app.state.store.close()


def _token_ok(request: Request, token: str | None) -> bool:
    if token is None or request.url.path == "/health":
        return True
    supplied = request.headers.get(TOKEN_HEADER, "")
    return hmac.compare_digest(supplied.encode(), token.encode())


def install_middleware(app: FastAPI, token: str | None) -> None:
    @app.middleware("http")
    async def log_and_auth(
        request: Request, call_next: Callable[[Request], Awaitable[Response]]
    ) -> Response:
        start = time.perf_counter()
        status = 500
        try:
            if not _token_ok(request, token):
                response: Response = err("missing or invalid X-Jukebox-Token", 401)
            else:
                response = await call_next(request)
            status = response.status_code
            return response
        finally:
            ms = (time.perf_counter() - start) * 1000
            log.info("%s %s %d %.1fms", request.method, request.url.path, status, ms)


def create_app(
    settings: Settings | None = None,
    fetcher: Fetcher | None = None,
    store: Store | None = None,
) -> FastAPI:
    configure_logging()
    settings = settings or Settings.from_env()
    store = store or Store(settings.db_path)
    fetcher = fetcher or Fetcher()
    app = FastAPI(
        title="Podcast Jukebox resolver",
        version=__version__,
        lifespan=lifespan,
        docs_url=None,
        redoc_url=None,
        openapi_url=None,
    )
    app.state.settings = settings
    app.state.store = store
    app.state.fetcher = fetcher
    app.state.channels = ChannelService(fetcher, store, settings.channels_url)
    app.state.episodes = EpisodeService(fetcher, store)
    app.state.started_at = time.time()
    install_middleware(app, settings.token)
    install_error_handlers(app)
    app.include_router(router)
    return app
