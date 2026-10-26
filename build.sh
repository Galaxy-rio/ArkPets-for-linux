#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake -S "$root" -B "${ARKPETS_BUILD_DIR:-$root/build}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "${ARKPETS_BUILD_DIR:-$root/build}" --parallel "${ARKPETS_BUILD_JOBS:-4}"
