#!/usr/bin/env bash
# Live smoke test: start the resolver, hit /health, /channels and /episodes (NPR News Now),
# print the first 3 episode titles, stop the server. Needs network. Not part of pytest.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UV="${UV:-uv}"
PORT="${SMOKE_PORT:-8799}"
BASE="http://127.0.0.1:${PORT}"
FEED="${SMOKE_FEED:-https://feeds.npr.org/500005/podcast.xml}"
WORK="$(mktemp -d)"
trap 'kill "${SERVER_PID:-}" 2>/dev/null || true; rm -rf "$WORK"' EXIT

JUKEBOX_DB="$WORK/smoke.db" "$UV" run --project "$HERE" uvicorn jukebox_resolver.server:app \
  --host 127.0.0.1 --port "$PORT" --no-access-log >"$WORK/server.log" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 60); do
  if curl -fsS "$BASE/health" >/dev/null 2>&1; then break; fi
  sleep 0.5
done
if ! curl -fsS "$BASE/health" >/dev/null 2>&1; then
  echo "server did not start:"; cat "$WORK/server.log"; exit 1
fi

echo "== /health"
curl -fsS "$BASE/health"; echo
echo "== /channels (count + first row)"
curl -fsS "$BASE/channels" | python3 -c 'import json,sys; d=json.load(sys.stdin)["data"]; print(len(d), "channels;", d[0])'
echo "== /episodes?feed=$FEED&limit=3"
curl -fsS --get "$BASE/episodes" --data-urlencode "feed=$FEED" --data-urlencode "limit=3" \
  | python3 -c '
import json, sys
body = json.load(sys.stdin)
assert body["ok"], body
for e in body["data"]:
    print(f"- {e['"'"'published'"'"']}  {e['"'"'title'"'"']}  ({e['"'"'duration_s'"'"']}s)\n    {e['"'"'url'"'"'][:100]}")
'
echo "== server log"
cat "$WORK/server.log"
