#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_prefix="${1:-$HOME/.local}"
"$root/build.sh"
cmake --install "${ARKPETS_BUILD_DIR:-$root/build}" --prefix "$app_prefix"
if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$app_prefix/share/applications"
fi
printf 'ArkPets 已安装到 %s\n从应用菜单打开 ArkPets，或运行 %s/bin/arkpets。\n' "$app_prefix" "$app_prefix"
