# snip

A small screen snipping tool for Windows and Linux, one wxWidgets program on
both. Windows: one ~4.5 MB exe (wx statically linked), no installer, no
services, no Store app, no runtime dependencies. Linux: a Flatpak; see
[Linux install/build instructions](docs/linux-build.md).

## Use (Windows)

Run `snip.exe`. It opens the Snip window: **New**, **Capture ▾** (Rectangle,
Window, Full desktop), **Copy**, **Save As...**, **Quit**, and at the right
**Support me on Ko-fi** and **About**. Running it again restores that window.
Closing the window keeps Snip in the tray.

**About** shows the version and a **Theme** choice: Same as system, Light or
Dark. On Windows the new theme applies the next time Snip starts; on Linux at once.

* **Start a snip:** **Win+Shift+S**, click **New**, click the tray icon,
  or run `snip.exe /snip` for immediate capture. `/tray` starts quietly in the tray.
  PrtScn and Ctrl+Alt+S are also registered when available; the tray tooltip lists
  active shortcuts. If Windows reserves Win+Shift+S, Snip handles that chord
  itself while running. Exiting Snip releases it; no system remapping is saved.
  Use **Start with Windows** in the tray menu if you want it available after login.
* **On the frozen screen:** drag a rectangle. The size in pixels shows as you
  drag. Enter = whole monitor under the cursor. Esc or right-click = cancel.
* **Capture a window:** press **W** on the frozen screen, hover to highlight an
  application window, then click to copy it. Press **R** to return to rectangle
  selection. You can also use **Capture ▾ > Window** (Ctrl+Shift+W) or
  **Capture > Window** in the tray menu. The capture includes the title bar and
  visible window area, excluding invisible resize borders. It captures what is
  on screen: overlapping windows remain visible and off-screen portions are
  clipped. Bring the desired window to the front first for an unobstructed snip.
* **After the snip:** the snip is already on the clipboard (PNG and bitmap), so
  you can paste it straight into Claude, a browser, Office or Paint. The window
  has **New** (Ctrl+N), **Copy** (Ctrl+C), **Save As...** (Ctrl+S, PNG),
  **Quit** (Ctrl+Q) and Esc to close.
* **Tray right-click:** New snip, Capture (Rectangle / Window / Full desktop),
  Show Snip, Start with Windows (an HKCU Run entry with `/tray`), Exit.

Spans all monitors, DPI-aware per monitor (snips are in real pixels).

If PrtScn does nothing, Windows has it reserved: turn off *Settings >
Accessibility > Keyboard > Use the Print screen key to open screen capture*,
then restart snip.

## Build

wxWidgets 3.3.4 is a git submodule in `vendor/wxWidgets`; after cloning run
`git submodule update --init --recursive`.

Windows: `build.bat` or `tools/build-windows.ps1` (Visual Studio 2022, x64,
Ninja, static CRT). Output: `dist/snip.exe`. The build runs a smoke test that
shows a synthetic snip in the real window (no capture, clipboard untouched).

Linux: see [docs/linux-build.md](docs/linux-build.md).

## Releases

`tools/build-all.ps1` builds everything from this Windows checkout (Linux through
WSL) and collects it in `dist/release/` with `SHA256SUMS.txt`, named with the
version in `CMakeLists.txt`:

| File | What it is |
| --- | --- |
| `Snip-<version>-windows-x64-setup.exe` | Installer: per user, no administrator rights, Start menu shortcut, optional desktop shortcut and start at sign-in. Closes a running Snip first; uninstall removes it and its sign-in entry. |
| `Snip-<version>-windows-x64-portable.zip` | `snip.exe` to run from any folder. |
| `Snip-<version>-linux-x86_64.AppImage` | Runs on Ubuntu 22.04 and newer (and SteamOS) without installing; uses the desktop's own GTK. |
| `Snip-<version>-linux-x86_64.flatpak` | Flatpak on the freedesktop 24.08 runtime. |

The steps on their own: `tools/build-windows.ps1`, `tools/build-installer.ps1`
(Inno Setup 6), `tools/build-linux-appimage.sh` and `tools/build-linux.sh` +
`tools/package-flatpak.sh` (in WSL; see the Linux doc), then
`tools/collect-release.ps1`. `-SkipLinux` builds only the Windows files.

Icons: `tools/make-icons.py` (Pillow) draws `packaging/icons` (Linux icon theme
PNGs and `snip.ico`) with the same design as the program's own drawing.

The UI is in `src/wx`. Capture, clipboard, shortcuts, the tray and the single
instance are platform code: `src/wx/platform_windows.cpp` and
`src/wx/platform_linux.cpp`.

`src/monitor_select.*` is a vendored copy of `C:\Source2026\shared\monitor_select`.
