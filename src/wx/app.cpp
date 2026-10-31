// FreeSnip start-up, tray menu and command line (/x, -x and --x all work):
//   (none)  open the FreeSnip window (or the running instance's)
//   --snip  start a capture immediately (also in the running instance)
//   --tray  start quietly in the tray ("Start with Windows" uses it)
//   --smoke-test  show a synthetic snip without capturing or touching the clipboard, then exit
//     --overlay          show a synthetic frozen screen with a selection instead
//     --about            open the About box over it
//     --screenshot PATH  save what the window draws as a PNG before exiting
#include "snip.h"
#include <wx/config.h>
#include <wx/dcscreen.h>
#include <wx/display.h>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <csignal>
#endif

namespace {

enum { ID_TRAY_SNIP = wxID_HIGHEST + 100, ID_TRAY_CAPTURE, ID_TRAY_RECTANGLE, ID_TRAY_WINDOW, ID_TRAY_DESKTOP,
       ID_TRAY_ACTIVE, ID_TRAY_OPTIONS, ID_TRAY_SHOW, ID_TRAY_CANCEL, ID_TRAY_STARTUP, ID_TRAY_EXIT };

snip::MainWindow* g_main = nullptr;

std::vector<snip::TrayItem> trayMenu()
{
 using snip::TrayItem;
 const wxString keys = snip::platform::shortcutNames();
 std::vector<TrayItem> items;
 items.push_back({ ID_TRAY_SNIP, keys.empty() ? wxString("New snip") : "New snip\t" + keys });
 TrayItem capture{ ID_TRAY_CAPTURE, "Capture" };
 capture.submenu.push_back({ ID_TRAY_RECTANGLE, "Rectangle" });
#ifdef __WXMSW__
 capture.submenu.push_back({ ID_TRAY_WINDOW, "Window" });
 capture.submenu.push_back({ ID_TRAY_DESKTOP, "Full desktop" });
#else
 capture.submenu.push_back({ ID_TRAY_DESKTOP, "Full desktop" });
 capture.submenu.push_back({ ID_TRAY_ACTIVE, "Active window" });
 capture.submenu.push_back({ ID_TRAY_OPTIONS, "Desktop options..." });
#endif
 items.push_back(capture);
 items.push_back({ ID_TRAY_SHOW, "Show FreeSnip" });
#ifndef __WXMSW__
 items.push_back({ ID_TRAY_CANCEL, "Cancel capture" });
#endif
 if (snip::platform::supportsStartup()) {
  items.push_back({});
  items.push_back({ ID_TRAY_STARTUP, "Start with Windows", true, snip::platform::startupEnabled() });
 }
 items.push_back({});
 items.push_back({ ID_TRAY_EXIT, "Exit" });
 return items;
}

void trayChosen(int id)
{
 if (!g_main) return;
 using snip::CaptureMode;
 switch (id) {
 case ID_TRAY_SNIP:
 case ID_TRAY_RECTANGLE: g_main->startCapture(CaptureMode::Rectangle, 200); break;
 case ID_TRAY_WINDOW: g_main->startCapture(CaptureMode::Window, 200); break;
 case ID_TRAY_DESKTOP: g_main->startCapture(CaptureMode::FullDesktop, 200); break;
 case ID_TRAY_ACTIVE: g_main->startCapture(CaptureMode::ActiveWindow, 200); break;
 case ID_TRAY_OPTIONS: g_main->startCapture(CaptureMode::DesktopOptions, 200); break;
 case ID_TRAY_SHOW: g_main->showMain(); break;
 case ID_TRAY_CANCEL: g_main->cancelCapture(); break;
 case ID_TRAY_STARTUP: snip::platform::setStartup(!snip::platform::startupEnabled()); break;
 case ID_TRAY_EXIT: snip::quitApp(); break;
 }
}

// Saves what a window draws as a PNG (smoke tests).
void saveWindow(wxTopLevelWindow* window, const wxString& path)
{
#ifdef __WXGTK__
 // Rendering the widget tree also works on Wayland, where the screen cannot be read back.
 GtkWidget* widget = window->GetHandle();
 const int w = gtk_widget_get_allocated_width(widget), h = gtk_widget_get_allocated_height(widget);
 cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
 cairo_t* cr = cairo_create(surface);
 gtk_widget_draw(widget, cr);
 cairo_destroy(cr);
 cairo_surface_write_to_png(surface, path.utf8_str());
 cairo_surface_destroy(surface);
#else
 window->Raise();
 window->Update();
 const wxRect area = window->GetScreenRect();
 wxBitmap bitmap(area.GetSize());
 {
  wxMemoryDC memory(bitmap);
  wxScreenDC screen;
  memory.Blit(0, 0, area.width, area.height, &screen, area.x, area.y);
 }
 bitmap.ConvertToImage().SaveFile(path, wxBITMAP_TYPE_PNG);
#endif
}

// A frozen screen over every monitor showing a gradient instead of the desktop.
snip::DesktopShot syntheticShot()
{
 snip::DesktopShot shot;
 wxRect bounds;
 for (unsigned i = 0; i < wxDisplay::GetCount(); ++i) {
  const wxRect g = wxDisplay(i).GetGeometry();
  shot.outputs.push_back({ g, {}, int(i) });
  bounds = bounds.IsEmpty() ? g : bounds.Union(g);
 }
 shot.image.Create(bounds.width, bounds.height, false);
 for (int y = 0; y < bounds.height; ++y)
  for (int x = 0; x < bounds.width; ++x)
   shot.image.SetRGB(x, y, 80 + 150 * x / bounds.width, 120 + 100 * y / bounds.height, 230);
 for (auto& o : shot.outputs) o.pixels = wxRect(o.geometry.GetPosition() - bounds.GetPosition(), o.geometry.GetSize());
 return shot;
}

}

namespace snip {
void quitApp()
{
 platform::stopTray();
 if (g_main) {
  auto* main = g_main;
  g_main = nullptr;
  main->Destroy();
 }
}
}

class Application : public wxApp {
 wxTimer smokeTimer_;
 std::unique_ptr<snip::FrozenDesktop> smokeOverlay_;
public:
 bool OnInit() override
 {
  bool tray = false, snipNow = false, smoke = false, overlay = false, about = false;
  wxString screenshot;
  for (int i = 1; i < argc; ++i) {
   const wxString a = wxString(argv[i]).Lower();
   if (a == "/tray" || a == "-tray" || a == "--tray") tray = true;
   if (a == "/snip" || a == "-snip" || a == "--snip") snipNow = true;
   if (a == "--smoke-test") smoke = true;
   if (a == "--overlay") overlay = true;
   if (a == "--about") about = true;
   if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
  }
#ifdef __WXGTK__
  // The Wayland app_id / X11 class must match the .desktop file for the taskbar icon.
  g_set_prgname("io.github.tcottrill.FreeSnip");
#endif
  if (!smoke && snip::platform::otherInstance(snipNow ? "snip" : tray ? "tray" : "show")) {
   // Handed over to the running copy: exit with success, without a window.
   SetExitOnFrameDelete(false);
   CallAfter([this] { ExitMainLoop(); });
   return true;
  }

  SetAppName("FreeSnip");
  SetVendorName("tcottrill");
  // Settings stay where Snip, the name before FreeSnip, kept them.
  wxConfigBase::Set(new wxConfig("Snip", "tcottrill"));
  SetAppearance(snip::savedAppearance());   // before the first window (Windows applies it only then)
  wxImage::AddHandler(new wxPNGHandler);
  g_main = new snip::MainWindow;
  SetTopWindow(g_main);

  if (smoke) {
   g_main->setInert();
   wxTopLevelWindow* shown = g_main;
   if (overlay) {
    snip::DesktopShot shot = syntheticShot();
    const wxRect first = shot.outputs.front().pixels;   // shown by the first surface
    smokeOverlay_ = std::make_unique<snip::FrozenDesktop>(std::move(shot), false, nullptr, nullptr);
    smokeOverlay_->show();
    smokeOverlay_->showSelection(wxRect(first.x + first.width / 4, first.y + first.height / 4, first.width / 3, first.height / 3));
    shown = smokeOverlay_->windows().front();
   } else {
    wxImage image(301, 151);
    image.SetRGB(wxRect(0, 0, 301, 151), 0x25, 0x63, 0xEB);
    g_main->showImage(image, wxDefaultPosition, false);
   }
   if (about) g_main->CallAfter([] { if (g_main) snip::showAbout(g_main); });
   smokeTimer_.Bind(wxEVT_TIMER, [this, shown, screenshot](wxTimerEvent&) {
    wxTopLevelWindow* target = shown;
    wxDialog* dialog = nullptr;
    for (auto* w : wxTopLevelWindows)
     if (auto* d = wxDynamicCast(w, wxDialog); d && d->IsShown()) target = dialog = d;
    if (!screenshot.empty()) saveWindow(target, screenshot);
    if (dialog) dialog->EndModal(wxID_OK);
    smokeOverlay_.reset();
    snip::quitApp();
   });
   smokeTimer_.StartOnce(screenshot.empty() ? 700 : 1500);
   return true;
  }

  snip::platform::startShortcuts(
   [] { if (g_main) g_main->startCapture(snip::CaptureMode::Rectangle); },
   [] { if (g_main) g_main->showMain(); });
  const wxString keys = snip::platform::shortcutNames();
  const bool trayShown = snip::platform::startTray(
   { [] { if (g_main) g_main->startCapture(snip::CaptureMode::Rectangle, 150); }, trayMenu, trayChosen },
   keys.empty() ? wxString("FreeSnip - click to snip") : "FreeSnip - click or " + keys);
  g_main->setKeepRunning(trayShown);   // without a tray, closing the window exits
#ifdef __WXGTK__
  // Logout and "Exit game" on SteamOS send SIGTERM: close normally.
  for (int signal : { SIGTERM, SIGHUP, SIGINT })
   g_unix_signal_add(signal, [](gpointer) -> gboolean { snip::quitApp(); return G_SOURCE_REMOVE; }, nullptr);
#endif
  if (snipNow) g_main->CallAfter([] { if (g_main) g_main->startCapture(snip::CaptureMode::Rectangle); });
  else if (!tray || !trayShown) g_main->showMain();
  return true;
 }
 int OnExit() override
 {
  snip::platform::stopShortcuts();
  return wxApp::OnExit();
 }
};

wxIMPLEMENT_APP(Application);
