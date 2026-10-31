#!/usr/bin/env bash
# Packages dist/linux/freesnip (built by tools/build-linux.sh in org.freedesktop.Sdk//24.08)
# as dist/linux/FreeSnip-x86_64.flatpak on the org.freedesktop.Platform 24.08 runtime.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ID=io.github.tcottrill.FreeSnip
WORK="${SNIP_FLATPAK_WORK:-$HOME/snip-flatpak}"
OUT="$ROOT/dist/linux"
PKG="$ROOT/packaging/flatpak"
mkdir -p "$WORK" "$OUT"
STAGE="$(mktemp -d "$WORK/stage-XXXXXX")"
flatpak build-init "$STAGE" "$ID" org.freedesktop.Sdk org.freedesktop.Platform 24.08
install -Dm755 "$OUT/freesnip" "$STAGE/files/bin/freesnip"
install -Dm644 "$PKG/$ID.desktop" "$STAGE/files/share/applications/$ID.desktop"
install -Dm644 "$PKG/$ID.metainfo.xml" "$STAGE/files/share/metainfo/$ID.metainfo.xml"
mkdir -p "$STAGE/files/share/icons" && cp -r "$ROOT/packaging/icons/hicolor" "$STAGE/files/share/icons/"
install -Dm644 "$ROOT/vendor/wxWidgets/docs/licence.txt" "$STAGE/files/share/licenses/freesnip/wxWidgets.txt"
# Screenshots, the global shortcut and Save As go through portals; the tray icon is a
# StatusNotifierItem, so the watcher is the one other service FreeSnip talks to.
flatpak build-finish --command=freesnip --share=ipc --socket=wayland --socket=fallback-x11 --device=dri \
  --talk-name=org.kde.StatusNotifierWatcher "$STAGE"
flatpak build-export "$WORK/repo" "$STAGE" preview
flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo "$WORK/repo" "$OUT/FreeSnip-x86_64.flatpak.new" "$ID" preview
mv "$OUT/FreeSnip-x86_64.flatpak.new" "$OUT/FreeSnip-x86_64.flatpak"
(cd "$OUT" && sha256sum FreeSnip-x86_64.flatpak > FreeSnip-x86_64.flatpak.sha256)
rm -rf "$STAGE"
echo "Flatpak: $OUT/FreeSnip-x86_64.flatpak"
