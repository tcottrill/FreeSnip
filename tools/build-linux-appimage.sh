#!/usr/bin/env bash
# Builds dist/linux/FreeSnip-x86_64.AppImage, the Linux release for systems without Flatpak.
#
# The build runs inside an Ubuntu 22.04 userland (glibc 2.35, GTK 3.24) started with
# bubblewrap, so the AppImage runs on Ubuntu 22.04 and newer even when this machine is
# newer. No root is needed: only bash, curl, tar and bwrap with unprivileged user
# namespaces. Every download is pinned by version and SHA-256. (The same method as
# tools/build-linux-appimage.sh in Skein Downloader.)
#
# wxWidgets (vendor/wxWidgets) is linked statically. GTK 3 and the libraries it brings
# (GLib/GIO, Pango, Cairo, Wayland, X11, libpng...) come from the system: every Ubuntu
# desktop and SteamOS has them, and the system copy keeps the desktop's theme, input
# methods and native Wayland, which a bundled GTK would lose. Downloads are cached in
# $SNIP_APPIMAGE_WORK (default ~/snip-appimage); delete it to start from scratch.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SNIP_APPIMAGE_WORK:-$HOME/snip-appimage}"
CACHE="$WORK/cache"
ROOTFS="$WORK/rootfs-ubuntu-22.04"
OUT="$ROOT/dist/linux"
APPIMAGE="FreeSnip-x86_64.AppImage"
ID=io.github.tcottrill.FreeSnip
[ -f "$ROOT/vendor/wxWidgets/CMakeLists.txt" ] || { echo "wxWidgets is missing: run git submodule update --init --recursive" >&2; exit 1; }
mkdir -p "$CACHE" "$WORK/build" "$OUT"

# fetch URL FILE SHA256: download into the cache once and always verify.
fetch() {
  local file="$CACHE/$2"
  if [ ! -f "$file" ] || ! echo "$3  $file" | sha256sum -c --status; then
    curl -fL --retry 3 -o "$file.part" "$1"
    mv "$file.part" "$file"
  fi
  echo "$3  $file" | sha256sum -c --quiet || { echo "Checksum mismatch: $2" >&2; exit 1; }
}
fetch https://cdimage.ubuntu.com/ubuntu-base/releases/22.04/release/ubuntu-base-22.04.5-base-amd64.tar.gz \
  ubuntu-base-22.04.5.tar.gz 242cd8898b33ea806ef5f13b1076ed7c76f9f989d18384452f7166692438ff1a
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
  linuxdeploy-x86_64.AppImage c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d

if [ ! -f "$ROOTFS/.extracted" ]; then
  rm -rf "$ROOTFS"; mkdir -p "$ROOTFS"
  tar -xzf "$CACHE/ubuntu-base-22.04.5.tar.gz" -C "$ROOTFS" --no-same-owner --exclude='./dev/*'
  touch "$ROOTFS/.extracted"
fi
rm -f "$ROOTFS/etc/resolv.conf"; cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"
mkdir -p "$ROOTFS/src" "$ROOTFS/cache" "$ROOTFS/build" "$ROOTFS/out"

# inroot SCRIPT: run as (namespaced) root inside the 22.04 userland.
inroot() {
  bwrap --unshare-user --uid 0 --gid 0 --bind "$ROOTFS" / --dev /dev --proc /proc --tmpfs /tmp \
    --ro-bind "$ROOT" /src --bind "$CACHE" /cache --bind "$WORK/build" /build --bind "$OUT" /out \
    --setenv HOME /root --setenv DEBIAN_FRONTEND noninteractive --setenv LANG C.UTF-8 \
    --setenv PATH /opt/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    bash -euo pipefail -c "$1"
}

if [ ! -f "$ROOTFS/.deps-v1" ]; then
  inroot '
    # apt cannot switch to its _apt user inside a user namespace, and only one uid/gid
    # is mapped there, so package scripts that chown to other groups (e.g. staff) would
    # fail. Ownership is irrelevant in this throwaway build root, so those calls may fail.
    echo "APT::Sandbox::User \"root\";" > /etc/apt/apt.conf.d/99namespace
    for tool in chown chgrp; do
      printf "#!/bin/sh\n/bin/%s \"\$@\" 2>/dev/null || true\n" "$tool" > /usr/local/sbin/$tool; chmod 755 /usr/local/sbin/$tool
    done
    # Some setup scripts still set ownership the namespace cannot grant, through dpkg-statoverride
    # (dbus) or an absolute /bin/chown (fontconfig-config) that the wrappers above miss. Those
    # steps do not matter here: nothing runs a D-Bus daemon or uses a system font cache. settle
    # skips only the scripts that actually failed (their packages are left half-configured,
    # "iF"), then configures again; dpkg --audit must end up clean.
    settle() {
      for attempt in 1 2 3 4; do
        dpkg --configure -a && return 0
        for pkg in $(dpkg-query -W -f="\${db:Status-Abbrev}|\${binary:Package}\n" | grep "^iF" | cut -d"|" -f2); do
          for script in /var/lib/dpkg/info/$pkg.postinst /var/lib/dpkg/info/${pkg%%:*}.postinst; do
            if [ -f "$script" ]; then echo "Skipping the setup script of $pkg"; printf "#!/bin/sh\nexit 0\n" > "$script"; fi
          done
        done
      done
      return 1
    }
    settle
    apt-get update
    # xvfb and xauth run the smoke test without a display.
    PACKAGES="ca-certificates g++-12 ninja-build pkg-config python3 python3-venv file
      libgtk-3-dev libpng-dev zlib1g-dev xvfb xauth"
    apt-get install -y --no-install-recommends $PACKAGES || { settle; apt-get install -y --no-install-recommends $PACKAGES; }
    test -z "$(dpkg --audit)" || { dpkg --audit; exit 1; }
    python3 -m venv /opt/venv
    pip install --no-cache-dir cmake==3.31.6
  '
  touch "$ROOTFS/.deps-v1"
fi

inroot '
  cmake -S /src -B /build/app -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-12 \
    -DCMAKE_CXX_COMPILER=g++-12 -DCMAKE_INSTALL_PREFIX=/usr -DSNIP_OUTPUT_DIR=/build/app/dist
  cmake --build /build/app

  rm -rf /build/AppDir /build/tools
  DESTDIR=/build/AppDir cmake --install /build/app
  mkdir -p /build/tools
  LICENSES=/build/AppDir/usr/share/licenses/freesnip; mkdir -p "$LICENSES"
  install -m644 /src/vendor/wxWidgets/docs/licence.txt "$LICENSES/wxWidgets.txt"
  install -Dm644 /src/packaging/flatpak/'"$ID"'.metainfo.xml /build/AppDir/usr/share/metainfo/'"$ID"'.appdata.xml

  install -m755 /cache/linuxdeploy-x86_64.AppImage /build/tools/
  export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 LDAI_OUTPUT=/build/'"$APPIMAGE"'
  # The desktop'"'"'s own GTK stack is used (see the top of this script).
  EXCLUDES=()
  for lib in "libgtk-3.so*" "libgdk-3.so*" "libgdk_pixbuf-2.0.so*" "libglib-2.0.so*" "libgobject-2.0.so*" \
      "libgio-2.0.so*" "libgmodule-2.0.so*" "libgthread-2.0.so*" "libpango-1.0.so*" "libpangocairo-1.0.so*" \
      "libpangoft2-1.0.so*" "libcairo.so*" "libcairo-gobject.so*" "libatk-1.0.so*" "libatk-bridge-2.0.so*" \
      "libatspi.so*" "libepoxy.so*" "libharfbuzz.so*" "libfribidi.so*" "libpixman-1.so*" "libpng16.so*" \
      "libwayland-*.so*" "libxkbcommon.so*" "libX*.so*" "libxcb*.so*" "libdbus-1.so*" "libffi.so*" \
      "libpcre*.so*" "libmount.so*" "libblkid.so*" "libselinux.so*" "libsystemd.so*"; do
    EXCLUDES+=(--exclude-library "$lib")
  done
  rm -f /build/'"$APPIMAGE"'
  cd /build/tools && ./linuxdeploy-x86_64.AppImage --appdir /build/AppDir "${EXCLUDES[@]}" \
    -e /build/AppDir/usr/bin/freesnip \
    -d /src/packaging/flatpak/'"$ID"'.desktop \
    -i /src/packaging/icons/hicolor/256x256/apps/'"$ID"'.png
  # The AppImage file'"'"'s own icon (.DirIcon, used by file managers): linuxdeploy links the
  # 64 px image, so the 256 px one replaces it before the second pass packs the AppImage.
  rm -f /build/AppDir/'"$ID"'.png /build/AppDir/.DirIcon
  install -m644 /src/packaging/icons/hicolor/256x256/apps/'"$ID"'.png /build/AppDir/'"$ID"'.png
  ln -s '"$ID"'.png /build/AppDir/.DirIcon
  ./linuxdeploy-x86_64.AppImage --appdir /build/AppDir "${EXCLUDES[@]}" --output appimage

  # Smoke test on the oldest supported base: the window opens and closes.
  APPIMAGE_EXTRACT_AND_RUN=1 xvfb-run -a /build/'"$APPIMAGE"' --smoke-test
  install -m755 /build/'"$APPIMAGE"' /out/'"$APPIMAGE"'
'
(cd "$OUT" && sha256sum "$APPIMAGE" > "$APPIMAGE.sha256")
echo "AppImage: $OUT/$APPIMAGE ($(du -h "$OUT/$APPIMAGE" | cut -f1))"
