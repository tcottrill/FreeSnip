# Linux test build

The Linux version uses Qt 6 Widgets. Windows retains the small native Win32
executable. The Linux Flatpak uses the same KDE 6.11 runtime as Downloader.

## Install on Ubuntu or SteamOS

Copy `dist/linux/Snip-x86_64.flatpak` to your Linux machine. In SteamOS use
Desktop Mode. Open the file in the software installer, or run:

```sh
flatpak install --user ./Snip-x86_64.flatpak
flatpak run io.github.tcottrill.Snip//preview
```

The first installation may download the shared KDE runtime from Flathub.
If Downloader already uses that runtime, Flatpak reuses it. This is a local
test candidate, not a published Flathub release. Installing a newer bundle
with the same command replaces the preview without deleting its settings.

## Capture and paste

- Launch Snip, or choose New (Ctrl+N) in its preview window.
- On Wayland, New requests a desktop snapshot (your desktop may ask permission).
  Snip covers each screen with its frozen contents. Drag any rectangle and
  release to copy it immediately. There is no separate windowed cropping step.
  Enter captures the screen under the pointer; Escape or
  right-click cancels. Dragging can extend across screen boundaries.
- On X11, drag directly on the frozen desktop. Enter captures the monitor
  under the pointer. Escape or right-click cancels.
- A completed snip automatically goes on the clipboard. Switch to Claude,
  a browser or another image-aware app and paste with Ctrl+V.
- Copy (Ctrl+C) copies again; Save As (Ctrl+S) writes a PNG.
- The Capture menu offers Rectangle, Full desktop, Active window, and Desktop
  options. Full desktop immediately copies the whole returned screenshot.
  Active window uses the desktop's direct active-window target where available
  (Screenshot portal v3 with target 8). Older desktops report this limitation;
  Desktop options preserves their interactive window/full-screen/area picker.
  Its returned image is copied immediately without another selection step.
- Closing the preview hides it when a tray is available; otherwise it exits.
  Keep Snip running until you have pasted: Linux clipboard contents may be lost
  when their owning application quits. Quit / Ctrl+Q always exits.

On desktops with tray support, click the tray icon for a new snip; right-click
for New, Show Snip and Exit. Without a tray, the preview remains the control
window. Launching Snip again tells the running instance to capture.

The Shortcut button asks the GlobalShortcuts portal to assign a capture key
(suggested Ctrl+Alt+S). Availability and the final key are desktop-controlled.
For a persistent desktop keyboard shortcut, assign this command in your
desktop's keyboard settings:

```sh
flatpak run io.github.tcottrill.Snip//preview --snip
```

It also launches Snip if it isn't already running. The app does not take over
Print Screen or modify desktop shortcuts. `--tray` starts without capturing.

## Display and portal behavior

Wayland uses the standard XDG screenshot portal; its permission dialogs and
supported capture targets depend on the desktop. Install your desktop's portal
backend if Snip reports that screenshot capture is unavailable. WSLg has a
Wayland display but does not provide a full Linux desktop screenshot backend.

Rectangle mode requests noninteractive full-desktop capture. A desktop that
requires a permission confirmation can still show it before the screen freezes.
Snip checks that the screenshot dimensions match the desktop layout before
placing fullscreen overlays. An incompatible screenshot/layout (for example,
a backend with a different mixed-DPI composition) produces an explicit error
and leaves Desktop options available, rather than displaying a misaligned image.
Changing monitors while selecting cancels the capture.

Cropping uses original image pixels. On X11,
mixed-scale monitors are composed at the highest monitor scale; lower-density
monitors are resampled. Equal-scale monitor captures preserve pixel density.
On Wayland, the desktop controls the resolution of the returned screenshot.

## Build

On Linux with Flatpak Builder and the KDE SDK installed:

```sh
flatpak install --user flathub org.kde.Sdk//6.11 org.kde.Platform//6.11
bash tools/build-linux-flatpak.sh
```

From this Windows checkout:

```powershell
wsl -d Ubuntu -- bash /mnt/c/Source2026/clip/tools/build-linux-flatpak.sh
```

Build cache and export repository are in `~/snip-flatpak`. Override with
`SNIP_FLATPAK_WORK`. Output is `dist/linux/Snip-x86_64.flatpak`, alongside
the SHA-256 file and build log. No application-specific background service,
network access permission, or broad home-directory permission is needed.

## Automated tests

With native Qt6 development packages, CMake, Ninja and Python:

```sh
dbus-run-session -- bash tools/test-linux.sh
```

Using the installed SDK from WSL:

```powershell
wsl -d Ubuntu -- dbus-run-session -- flatpak run --socket=session-bus --env=QT_QPA_PLATFORM=offscreen --command=bash --filesystem=/mnt/c/Source2026/clip org.kde.Sdk//6.11 /mnt/c/Source2026/clip/tools/test-linux.sh
```

Full session-bus access is only used for tests that register fake portal
services inside an isolated bus; the shipped app uses Flatpak's normal filtered
bus. The tests cover selection geometry, PNG/clipboard data, mouse/keyboard
selection, fake screenshot/shortcut portals, PNG saving through the UI,
capture cancellation and launching a second instance.

Frozen-desktop regressions also cover fullscreen rectangle selection, monitor
coordinates, pointer-based monitor selection, invalid surface placement, and
keeping the preview hidden during capture. The Wayland UI integration test uses
a simulated screenshot portal and checks the exact cropped clipboard pixels.

Packaged startup check (synthetic image, exits automatically):

```sh
flatpak run io.github.tcottrill.Snip//preview --smoke-test
```

## On-device checks for this candidate

- KDE/SteamOS and GNOME/Ubuntu: capture, cancel, then capture again.
- Paste a cropped snip into Claude/browser and an image editor.
- Save to Pictures and a folder chosen through the desktop file chooser.
- Try multiple monitors, different scaling, and monitors left of the primary.
- Test the tray, desktop shortcut and portal Shortcut button.

Automated tests and WSL startup cannot establish those real-desktop results.
