#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$root/bin/arkpets" ]; then "$root/build.sh"; fi
exec "$root/bin/arkpets" "$@"
