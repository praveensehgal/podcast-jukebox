"""Response envelope and error handling shared by the app and routes."""

from __future__ import annotations

import logging
from typing import Any

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

from .feeds import FeedError
from .fetcher import FetchError

log = logging.getLogger("jukebox.app")


class ApiError(Exception):
    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


def ok(data: Any, status: int = 200) -> JSONResponse:
    return JSONResponse({"ok": True, "data": data}, status_code=status)


def err(message: str, status: int) -> JSONResponse:
    return JSONResponse({"ok": False, "error": message}, status_code=status)


def _validation_message(exc: RequestValidationError) -> str:
    first = next(iter(exc.errors()), None)
    if not first:
        return "invalid request"
    where = ".".join(str(p) for p in first.get("loc", ()) if p != "body")
    return f"{where}: {first.get('msg', 'invalid')}" if where else str(first.get("msg"))


def install_error_handlers(app: FastAPI) -> None:
    @app.exception_handler(ApiError)
    async def _api_error(_: Request, exc: ApiError) -> JSONResponse:
        return err(exc.message, exc.status)

    @app.exception_handler(RequestValidationError)
    async def _validation(_: Request, exc: RequestValidationError) -> JSONResponse:
        return err(_validation_message(exc), 400)

    @app.exception_handler(StarletteHTTPException)
    async def _http(_: Request, exc: StarletteHTTPException) -> JSONResponse:
        return err(str(exc.detail), exc.status_code)

    @app.exception_handler(FetchError)
    async def _fetch(_: Request, exc: FetchError) -> JSONResponse:
        return err(f"upstream fetch failed: {exc}", 502)

    @app.exception_handler(FeedError)
    async def _feed(_: Request, exc: FeedError) -> JSONResponse:
        return err(f"bad feed: {exc}", 502)

    @app.exception_handler(Exception)
    async def _unhandled(_: Request, exc: Exception) -> JSONResponse:
        log.exception("unhandled error: %s", exc)
        return err("internal error", 500)
