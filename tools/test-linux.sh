#!/bin/sh
# Runs the Linux D-Bus integration test (fake screenshot portal and tray watcher on a
# private session bus) against dist/linux/freesnip inside org.freedesktop.Sdk//24.08.
# Needs dbus-run-session and Flatpak on the host, and a Wayland display (WSLg works).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec dbus-run-session -- flatpak run --socket=session-bus --socket=wayland --filesystem="$root" \
  --command=python3 org.freedesktop.Sdk//24.08 "$root/tests/linux_dbus_test.py" "$root/dist/linux/freesnip"
