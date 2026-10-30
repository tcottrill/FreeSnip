# Linux candidate verification - 2026-10-21

Historical 1.0 candidate. See [1.1 frozen-desktop verification](wayland-frozen-verification.md)
for the replacement bundle and current results.

Deliverable: `dist/linux/Snip-x86_64.flatpak` (64,956 bytes).
SHA-256: `6d13f5c28e0060eadee799c244b9c792f14e9abb0ce48a841cb8ad6bb8fa0a38`.
Exported and installed Flatpak commit:
`c415656e80fa8ad3e55a7c51cc9afc9436a8a08c23a53e90a594d93a97e4eb31`.

## Completed

- Linux Debug build with GCC 15.2 / Qt 6.11.2 in org.kde.Sdk 6.11.
- Six CTest suites passed (2.61 seconds): instance_test, image_tests,
  selection_tests, portal_tests, shortcut_tests and ui_tests.
- Tests cover crop bounds/reversal/scaling, PNG round trip, clipboard MIME
  data, mouse/keyboard selection, monitor selection, portal success/cancel/
  invalid URI/timeout/missing service, shortcut bind/activation/cancel,
  preview copying, exact-filename PNG saving and capture cancellation.
- Second process activation is acknowledged by the original process.
- UI preview and selection widgets rendered to PNG and inspected.
- Separate code review found two issues; both reproduced with failing tests
  and fixed: preserve the exact file-chooser path, and keep the preview hidden
  while portal capture is pending.
- Flatpak Release build/export succeeded with AppStream metadata composition.
- Bundle installed in WSL; installed commit matches export.
- Installed bundle launched successfully with QT_QPA_PLATFORM=wayland and
  --smoke-test (synthetic preview, automatic exit, exit status 0).
- Original Windows source built successfully with MSVC 19.51 in
  `build/windows-check/Release/snip.exe`. The initial build.bat attempt could
  not replace the root snip.exe (LNK1104); the separate build verified the
  native source without needing to replace that executable.
- Windows and Linux SHA-256 calculations matched for the exported bundle.

## Limits and implementation decisions

- Screenshot and shortcut protocol tests use fake D-Bus services. WSLg startup
  does not test a real KDE/GNOME screenshot picker, permission dialog, physical
  monitor layout, or capture/paste into an external application.
- The native Save dialog still needs KDE/GNOME on-device testing. The UI
  save test uses Qt's non-native dialog and checks actual file content.
- X11 mixed-DPI desktop composition uses the highest screen scale; lower-DPI
  screens are resampled. Wayland crop resolution follows the returned image.
- Portal capture remains hidden until completion/cancellation; cancellation
  is available from the portal itself and from the tray. A missing response
  times out after two minutes and restores the preview.
- Global shortcut sessions last while the application is running. The
  Shortcut button can enable one each run; a desktop-configured launcher
  shortcut is the persistent alternative and starts Snip as needed.
- No Git repository was present, so changes were made directly in the user-
  authorized workspace. Downloader was inspected but not modified.

See `linux-build.md` for installation and the real-device checklist.
