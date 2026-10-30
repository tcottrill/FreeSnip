#!/bin/sh
# Builds dist/linux/snip (wxGTK 3, static wx). Run inside org.freedesktop.Sdk//24.08
# (see docs/linux-build.md) or on a system with GTK 3 development files, CMake and Ninja.
# The smoke test runs when a display is available (WAYLAND_DISPLAY or DISPLAY).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${SNIP_BUILD_DIR:-$root/build/linux}
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DSNIP_OUTPUT_DIR=$root/dist/linux"
cmake --build "$build" --parallel "${SNIP_JOBS:-8}"
if [ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then
  ctest --test-dir "$build" --output-on-failure
else
  echo "No display: smoke test skipped"
fi
