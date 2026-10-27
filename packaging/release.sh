#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
release_dir="$root/outputs"
version=0.9.6
mkdir -p "$release_dir"
archive="$release_dir/arkpets-linux-$version.tar.gz"
# Explicit allowlist: downloaded models, caches, old prototypes and binaries stay out.
tar -czf "$archive" --transform "s,^,arkpets-linux-$version/," \
    --exclude='__pycache__' --exclude='*.pyc' -C "$root" \
    CMakeLists.txt LICENSE README.md THIRD_PARTY.md SOURCES.json .gitignore AGENTS.md \
    build.sh run.sh stop.sh install.sh src qml resources vendor tests packaging
archive_hash=$(sha256sum "$archive" | cut -d ' ' -f 1)
sed "s/@SOURCE_SHA256@/$archive_hash/" "$root/packaging/PKGBUILD.in" > "$release_dir/PKGBUILD"
printf 'Source archive and PKGBUILD: %s\n' "$release_dir"
printf 'Build the Arch package with: cd "%s" && makepkg\n' "$release_dir"
