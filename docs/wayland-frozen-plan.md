# Wayland frozen rectangle correction

User authorized implementation through tested Flatpak delivery on 2026-10-21.
Root cause: interactive=true for every request and showMaximized for returned
images made the default workflow a separate image crop, not screen selection.

Implementation:
- Default rectangle: request noninteractive desktop screenshot, then fullscreen
  frozen selection surfaces, one per output (Wayland cannot place a single
  spanning top-level). Shared coordinates allow dragging across outputs.
- Map screenshot pixels to the logical desktop union without fitting into a
  preview window. Reject incompatible snapshot/layout ratios instead of silently
  distorting the screen. Cancel if outputs change during capture/selection.
- Full desktop copies the entire screenshot; Enter selects an output in rectangle
  mode. Desktop options retains the old interactive portal picker; its result
  copies immediately. Active window requests v3 target 8 only if advertised.
  Older desktops retain their interactive window choices through Desktop options.
- Test portal request modes, fullscreen drag-to-clipboard, cancellation, monitor
  coordinates, and retained capture choices. Run suite, package and smoke-check.
- Permission dialogs are compositor policy, not something this application can
  suppress. WSLg can test native fullscreen UI with a simulated screenshot portal;
  it cannot prove real KDE/GNOME screenshot backend behavior.
