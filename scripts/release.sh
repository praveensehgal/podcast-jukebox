#!/usr/bin/env bash
# Cut a firmware release: scripts/release.sh 0.2.0
# Tags the current commit vX.Y.Z and pushes it; GitHub Actions builds and publishes.
set -euo pipefail

ver="${1:-}"
if [[ ! "$ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "usage: $0 X.Y.Z" >&2
  exit 1
fi
if [[ -n "$(git status --porcelain)" ]]; then
  echo "working tree not clean; commit first" >&2
  exit 1
fi
git tag -a "v$ver" -m "firmware v$ver"
git push origin "v$ver"
echo "pushed v$ver. Watch: gh run watch"
