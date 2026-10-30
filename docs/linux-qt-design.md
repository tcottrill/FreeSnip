# Linux Qt port

The Wayland interaction below was superseded by the user's explicit correction
and `wayland-frozen-plan.md`: New must use fullscreen frozen-screen rectangle
selection. Windowed cropping is no longer the default Wayland workflow.

Use the Downloader Linux port as the packaging reference: Qt 6 Widgets,
CMake, and an x86_64 Flatpak using org.kde.Platform / org.kde.Sdk 6.11.
Target Ubuntu and SteamOS desktop mode. These targets and packaging choices
are inferred from the user's request to follow the Downloader approach.

## Behavior

Keep the existing Windows executable and build.bat. Add a Linux frontend
with New, Copy, Save As PNG, image preview, pixel dimensions, and keyboard
shortcuts within the preview. Successful captures automatically copy image/png
and Qt image data to the clipboard. Keep the process running while it owns
the clipboard; provide an explicit Quit action.

On X11, capture the desktop and show a frozen selection overlay, with a
dimension readout, drag selection, Escape cancellation and monitor capture.
Account for negative monitor coordinates and display scaling. Document any
resampling needed for selections spanning monitors with different scales.

On Wayland, request a screenshot through the XDG Screenshot portal. The
desktop owns permission and capture UI, so the flow cannot promise the same
overlay or one-step interaction on every desktop. Provide cropping of the
returned image inside Snip, mapping preview coordinates to original image
pixels. Cancellation preserves the last capture and clipboard. Missing or
failed portal requests show an actionable error rather than a blank image.

Provide a tray menu where supported, and a visible window when no tray is
available. Launching again requests a new snip from the existing process.
Provide a --snip launcher command for desktop-configured global shortcuts;
use the GlobalShortcuts portal where the desktop supports it. Do not assume
Print Screen is available or overwrite existing desktop shortcuts.

Use desktop file chooser integration for PNG saves, remember the destination,
and report write failures. Do not add background services. Linux depends on
the Qt runtime and standard desktop portals; it cannot match the 210 KB Windows
binary's dependency footprint. The runtime can be shared with Downloader.

## Structure and delivery

Add src/qt for the Linux application, with separate capture/portal handling,
selection and image operations, and preview/tray orchestration. Add CMake
platform selection and Flatpak metadata/build tooling following Downloader.
Do not introduce Qt into the Windows build or change Downloader files.

Deliver dist/linux/Snip-x86_64.flatpak, build/install instructions, and a
verification record stating which desktop checks remain untested.

## Verification

Build the existing Windows target and the Linux target. Test coordinate mapping,
reverse-direction selection, bounds, PNG round trips, clipboard MIME data,
capture cancellation, portal error/response handling and single-instance
activation. Run Qt tests offscreen where possible. Build the Flatpak and
perform a startup smoke check. Actual host desktop capture and cross-app paste,
mixed-DPI monitors, KDE/GNOME portal dialogs and Steam Deck operation need a
real desktop session; WSL startup alone does not validate them.

## Builder check

The screenshot portal returns an image URI rather than guaranteed monitor
geometry. Therefore Wayland cropping is image-based, not a global-coordinate
overlay. Clipboard ownership requires a live process. Tray availability and
global shortcut portal availability must be detected independently. These
constraints are included above; no additional feature decisions are required.
