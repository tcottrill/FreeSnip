#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP=io.github.tcottrill.Snip
WORK="${SNIP_FLATPAK_WORK:-$HOME/snip-flatpak}"
OUT="$ROOT/dist/linux"
mkdir -p "$WORK" "$OUT"
cd "$WORK"
flatpak-builder --user --force-clean --disable-rofiles-fuse --repo="$WORK/repo" --default-branch=preview "$WORK/app" "$ROOT/packaging/flatpak/$APP.json" 2>&1 | tee "$OUT/build.log"
flatpak build-bundle "$WORK/repo" "$OUT/Snip-x86_64.flatpak.new" "$APP" preview --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo
mv "$OUT/Snip-x86_64.flatpak.new" "$OUT/Snip-x86_64.flatpak"
cd "$OUT"
sha256sum Snip-x86_64.flatpak > Snip-x86_64.flatpak.sha256
echo "Test bundle: $OUT/Snip-x86_64.flatpak"
