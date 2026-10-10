"""HTTP endpoints. Every response is {"ok": true, "data": ...} or {"ok": false, "error": ...}."""

from __future__ import annotations

import re
import time

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field, field_validator

from . import __version__
from .config import DEFAULT_EPISODES, MAX_EPISODES
from .errors import ApiError, ok
from .fetcher import BlockedUrlError, check_url

router = APIRouter()
ID_RE = re.compile(r"^[A-Za-z0-9:_-]{1,64}$")
ID_RULE = "must be 1..64 chars of [A-Za-z0-9:_-]"


def require_id(value: str | None, name: str) -> str:
    if value is None or not ID_RE.match(value):
        raise ApiError(400, f"{name} {ID_RULE}")
    return value


def require_http_url(value: str | None, name: str) -> str:
    try:
        check_url(value or "")
    except BlockedUrlError as exc:
        raise ApiError(400, f"{name} refused: {exc}") from exc
    return value or ""


def clamp_limit(raw: str | None) -> int:
    if raw is None:
        return DEFAULT_EPISODES
    try:
        value = int(raw)
    except ValueError as exc:
        raise ApiError(400, "limit must be an integer") from exc
    return max(1, min(MAX_EPISODES, value))


class PositionIn(BaseModel):
    device: str
    episode: str
    pos_s: int = Field(ge=0)
    duration_s: int | None = Field(default=None, ge=0)

    @field_validator("device", "episode")
    @classmethod
    def _check_id(cls, value: str) -> str:
        if not ID_RE.match(value):
            raise ValueError(ID_RULE)
        return value


@router.get("/health")
async def health(request: Request) -> JSONResponse:
    state = request.app.state
    return ok(
        {
            "version": __version__,
            "channels": state.channels.cached_count(),
            "uptime_s": int(time.time() - state.started_at),
        }
    )


@router.get("/channels")
async def channels(request: Request) -> JSONResponse:
    return ok(await request.app.state.channels.get())


@router.get("/episodes")
async def episodes(request: Request) -> JSONResponse:
    params = request.query_params
    feed = require_http_url(params.get("feed"), "feed")
    limit = clamp_limit(params.get("limit"))
    return ok(await request.app.state.episodes.get(feed, limit))


@router.get("/position")
async def get_position(request: Request) -> JSONResponse:
    params = request.query_params
    device = require_id(params.get("device"), "device")
    episode = require_id(params.get("episode"), "episode")
    row = request.app.state.store.get_position(device, episode)
    return ok(row or {"pos_s": 0, "duration_s": None, "updated": None})


@router.post("/position")
async def post_position(request: Request, body: PositionIn) -> JSONResponse:
    row = request.app.state.store.upsert_position(
        body.device, body.episode, body.pos_s, body.duration_s
    )
    return ok(row)


@router.get("/positions")
async def positions(request: Request) -> JSONResponse:
    device = require_id(request.query_params.get("device"), "device")
    return ok(request.app.state.store.list_positions(device))


@router.get("/resolve")
async def resolve(request: Request) -> JSONResponse:
    url = require_http_url(request.query_params.get("url"), "url")
    return ok({"url": await request.app.state.fetcher.resolve(url)})
