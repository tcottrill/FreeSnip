# Wayland frozen-desktop candidate 1.1.0 - 2026-10-21

Deliverable: `dist/linux/Snip-x86_64.flatpak` (78,656 bytes).
SHA-256: `d07b385257c794c7fcdf72b33c2fbb751e92097babfa4c5a3554bded7a741121`.
Exported and installed Flatpak commit:
`02b2610b95643d45d140a5c4fc461542544e619072891e7b4e85fdf294fd2f2c`.

## Implemented

New requests a desktop snapshot, displays frozen fullscreen surfaces on each
monitor, and copies the arbitrary rectangle immediately on mouse release.
Enter captures the monitor under the pointer. Escape/right-click cancel.
Capture retains full-desktop, active-window and desktop-provided interactive
options. Direct active-window capture requires portal support; Desktop options
retains the desktop's picker when direct capture is unavailable.

## Verification

- Linux build and Flatpak export completed successfully using KDE SDK 6.11.
- All seven CTest suites passed: frozen, instance, image, selection, portal,
  shortcut and UI (4.11 seconds total).
- Native WSLg Wayland UI integration passed (3.599 seconds), with a simulated
  screenshot portal. It checks fullscreen display, drag/release, exact clipboard
  crop pixels and the other capture modes. The test waits for surface exposure
  before sending simulated mouse input.
- Frozen fullscreen rendering was saved to PNG and visually inspected.
- Windows Release build passed; the native Windows implementation is unchanged.
- Review findings were reproduced and fixed: validate surface placement,
  suppress preview during capture, and select Enter's monitor using the pointer.
  Follow-up review reported no remaining findings in these fixes.
- Final bundle installed successfully. Installed commit matches the export,
  `--version` reports `Snip 1.1.0`, and packaged Wayland `--smoke-test` exits 0.
- Bundle SHA-256 independently checked against the generated checksum file.

## Remaining on-device validation

WSLg does not supply a full KDE/GNOME screenshot backend. Native Wayland UI
testing uses a synthetic screenshot source and cannot establish real desktop
portal capture, external-app paste, or multi-monitor compositor behavior.
Try New, drag/release, then Ctrl+V into Claude on the target Linux desktop.
Also exercise PNG saving, full desktop, active window/Desktop options, and
multiple monitors. Desktop permission prompts may still appear. Incompatible
screenshot/layout scaling is rejected explicitly rather than stretched.
