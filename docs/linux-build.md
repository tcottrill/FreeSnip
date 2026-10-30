# Linux build

Snip uses the same wxWidgets code on Linux as on Windows (wxGTK 3, wx 3.3.4
statically linked). The Flatpak runs on the freedesktop 24.08 runtime, the same
as Skein Downloader. Linux-specific code is in `src/wx/platform_linux.cpp`.

## Install on Ubuntu or SteamOS

Copy `dist/linux/Snip-x86_64.flatpak` to your Linux machine. In SteamOS use
Desktop Mode. Open the file in the software installer, or run:

```sh
flatpak install --user ./Snip-x86_64.flatpak
flatpak run io.github.tcottrill.Snip//preview
```

The first installation may download the freedesktop 24.08 runtime from Flathub
(Downloader already uses it). This is a local test build, not a Flathub release.
Installing a newer bundle the same way replaces it.

## Capture and paste

- Launch Snip from the menu (it starts a snip), or choose New (Ctrl+N) in its window.
- On Wayland, Snip asks the desktop for a screenshot through the XDG screenshot
  portal (your desktop may ask permission), then covers each screen with its
  frozen contents. Drag a rectangle and release to copy it. Enter takes the
  screen under the pointer; Escape or right-click cancels.
- On X11 Snip reads the screen itself and shows the same frozen screen.
- A finished snip is on the clipboard (image/png) at once; paste it with Ctrl+V.
  Copy (Ctrl+C) copies again; Save As (Ctrl+S) writes a PNG through the desktop's
  file chooser.
- Capture offers Rectangle, Full desktop, Active window and Desktop options.
  Active window needs a desktop whose portal offers it directly; Desktop options
  opens the desktop's own picker. Their results are copied without a selection step.
- Window picking with W on the frozen screen is Windows-only (Wayland gives
  applications no window list).
- Closing the window hides it when a tray is available; otherwise Snip exits.
  Keep Snip running until you have pasted: Linux clipboards empty when the
  owning program quits. Quit (Ctrl+Q) always exits.

The tray icon is a StatusNotifierItem (KDE Plasma and SteamOS show it; GNOME
needs the AppIndicator extension). Click it for a new snip; its menu has New,
Capture, Show Snip, Cancel capture and Exit. Launching Snip again tells the
running copy to snip; `--tray` starts it quietly.

**Shortcut...** asks the desktop's GlobalShortcuts portal for a capture key
(suggested Ctrl+Alt+S); the desktop decides the final key. Or give this command
a shortcut in your desktop's keyboard settings:

```sh
flatpak run io.github.tcottrill.Snip//preview --snip
```

Snip never takes over Print Screen or changes desktop shortcuts.

## Display and portal behavior

Rectangle mode asks the portal for a non-interactive full-desktop screenshot.
Snip checks that the image matches the monitor layout before covering the
screens, and that each frozen screen was placed exactly over its monitor before
it accepts a selection; otherwise it cancels with a message rather than copying
the wrong pixels. Snips are cut from the screenshot's own pixels, so scaled
displays keep their full resolution.

WSLg has a Wayland display but no screenshot portal backend, so capture itself
cannot be tried there.

## Build

The binary is built inside the freedesktop SDK and packaged with
`flatpak build-init`, as for Downloader. From this Windows checkout:

```powershell
wsl -d Ubuntu -e bash -c 'cd /mnt/c/Source2026/clip && flatpak run --filesystem=/mnt/c/Source2026/clip --filesystem=home --socket=wayland --env=SNIP_BUILD_DIR=$HOME/snip-build --command=sh org.freedesktop.Sdk//24.08 tools/build-linux.sh'
wsl -d Ubuntu -e bash /mnt/c/Source2026/clip/tools/package-flatpak.sh
```

`tools/build-linux.sh` writes `dist/linux/snip` and runs the smoke test when a
display is available. `SNIP_BUILD_DIR` keeps the build tree on the Linux side,
which is much faster than `/mnt/c`. `tools/package-flatpak.sh` writes
`dist/linux/Snip-x86_64.flatpak` and its SHA-256 file; its work folder is
`~/snip-flatpak` (`SNIP_FLATPAK_WORK`). The SDK and runtime come from Flathub:

```sh
flatpak install --user flathub org.freedesktop.Sdk//24.08 org.freedesktop.Platform//24.08
```

The Flatpak needs only Wayland/X11, IPC and the GPU, plus talking to
`org.kde.StatusNotifierWatcher` for the tray icon. Screenshots, the global
shortcut and Save As go through portals.

## Tests

```sh
sh tools/test-linux.sh
```

Runs `tests/linux_dbus_test.py` on a private session bus with a fake screenshot
portal and tray watcher. It checks the tray item and menu, a portal capture
started from the menu, handing `--snip` from a second launch to the running copy,
and Exit. Needs `dbus-run-session` and a Wayland display (WSLg works).

Smoke runs (synthetic image, no capture, no clipboard; exit by themselves):

```sh
flatpak run io.github.tcottrill.Snip//preview --smoke-test
dist/linux/snip --smoke-test --overlay --screenshot overlay.png
```

`--overlay` shows a frozen screen with a sample selection instead of the window;
`--screenshot` saves what the window draws as a PNG, which also works on Wayland.

## Checks that need a real desktop

- KDE/SteamOS and GNOME/Ubuntu: capture, cancel, then capture again.
- Paste a snip into Claude or a browser and into an image editor.
- Save to Pictures and to a folder chosen in the file chooser.
- Several monitors, different scaling, monitors left of the primary.
- The tray icon and menu, a desktop shortcut and the Shortcut button.
