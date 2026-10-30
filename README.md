# snip

**Linux:** a Qt 6 version and Flatpak test package are now available. See
[Linux install/build instructions](docs/linux-build.md). The details below
describe the original Windows version.

A tiny screen snipping tool: one ~200 KB exe, no installer, no services, no
Store app, no runtime dependencies.

## Use

Run `snip.exe`. It opens the capture GUI with **New** and **Window** controls.
Running it again restores that window. Closing the window keeps Snip in the tray.

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
  selection. You can also use **Window** in the preview (Ctrl+Shift+W) or
  **Capture window** in the tray menu. The capture includes the title bar and
  visible window area, excluding invisible resize borders. It captures what is
  on screen: overlapping windows remain visible and off-screen portions are
  clipped. Bring the desired window to the front first for an unobstructed snip.
* **After the snip:** the snip is already on the clipboard (PNG and bitmap), so
  you can paste it straight into Claude, a browser, Office or Paint. The preview
  window has **New** (Ctrl+N), **Copy** (Ctrl+C), **Save As...** (Ctrl+S, PNG)
  and Esc to close.
* **Tray right-click:** New snip, Capture window, Open Snip, Start with Windows (an HKCU
  Run entry with `/tray`), Exit.

Spans all monitors, DPI-aware per monitor (snips are in real pixels).

If PrtScn does nothing, Windows has it reserved: turn off *Settings >
Accessibility > Keyboard > Use the Print screen key to open screen capture*,
then restart snip.

## Build

`build.bat` (Visual Studio 2022, x64, static CRT). Output: `snip.exe`.

Native Windows capture regression tests (run on an interactive desktop; they
briefly display test windows and replace the clipboard with a test capture):

```powershell
cmake -S . -B build/windows-check
cmake --build build/windows-check --config Release
ctest --test-dir build/windows-check -C Release --output-on-failure
```

`src/monitor_select.*` is a vendored copy of `C:\Source2026\shared\monitor_select`.
