// Snip start-up, tray icon and command line:
//   (none)  open the Snip window (or the running instance's)
//   /snip   start a capture immediately
//   /tray   start quietly in the tray (used by "Start with Windows")
//   --smoke-test  show a synthetic snip without capturing or touching the clipboard, then exit
#include "snip.h"
#include <wx/taskbar.h>
#include <wx/config.h>

namespace {

enum { ID_TRAY_SNIP = wxID_HIGHEST + 100, ID_TRAY_RECTANGLE, ID_TRAY_WINDOW, ID_TRAY_DESKTOP,
       ID_TRAY_SHOW, ID_TRAY_STARTUP, ID_TRAY_EXIT };

snip::MainWindow* g_main = nullptr;
wxTaskBarIcon* g_tray = nullptr;

class Tray : public wxTaskBarIcon {
public:
 Tray()
 {
  Bind(wxEVT_TASKBAR_LEFT_UP, [](wxTaskBarIconEvent&) { if (g_main) g_main->startCapture(snip::CaptureMode::Rectangle, 150); });
  auto on = [this](int id, std::function<void()> action) { Bind(wxEVT_MENU, [action](wxCommandEvent&) { if (g_main) action(); }, id); };
  on(ID_TRAY_SNIP, [] { g_main->startCapture(snip::CaptureMode::Rectangle, 200); });
  on(ID_TRAY_RECTANGLE, [] { g_main->startCapture(snip::CaptureMode::Rectangle, 200); });
  on(ID_TRAY_WINDOW, [] { g_main->startCapture(snip::CaptureMode::Window, 200); });
  on(ID_TRAY_DESKTOP, [] { g_main->startCapture(snip::CaptureMode::FullDesktop, 200); });
  on(ID_TRAY_SHOW, [] { g_main->showMain(); });
  on(ID_TRAY_STARTUP, [] { snip::platform::setStartup(!snip::platform::startupEnabled()); });
  on(ID_TRAY_EXIT, [] { snip::quitApp(); });
 }
protected:
 wxMenu* CreatePopupMenu() override
 {
  auto menu = new wxMenu;
  const wxString keys = snip::platform::shortcutNames();
  menu->Append(ID_TRAY_SNIP, keys.empty() ? wxString("New snip") : "New snip\t" + keys);
  auto capture = new wxMenu;
  capture->Append(ID_TRAY_RECTANGLE, "Rectangle");
  capture->Append(ID_TRAY_WINDOW, "Window");
  capture->Append(ID_TRAY_DESKTOP, "Full desktop");
  menu->AppendSubMenu(capture, "Capture");
  menu->Append(ID_TRAY_SHOW, "Show Snip");
  if (snip::platform::supportsStartup()) {
   menu->AppendSeparator();
   menu->AppendCheckItem(ID_TRAY_STARTUP, "Start with Windows")->Check(snip::platform::startupEnabled());
  }
  menu->AppendSeparator();
  menu->Append(ID_TRAY_EXIT, "Exit");
  return menu;
 }
};

}

namespace snip {
void quitApp()
{
 if (g_tray) {
  g_tray->RemoveIcon();
  g_tray->Destroy();
  g_tray = nullptr;
 }
 if (g_main) {
  auto* main = g_main;
  g_main = nullptr;
  main->Destroy();
 }
}
}

class Application : public wxApp {
 wxTimer smokeTimer_;
public:
 bool OnInit() override
 {
  bool tray = false, snipNow = false, smoke = false;
  for (int i = 1; i < argc; ++i) {
   const wxString a = wxString(argv[i]).Lower();
   if (a == "/tray" || a == "-tray" || a == "--tray") tray = true;
   if (a == "/snip" || a == "-snip" || a == "--snip") snipNow = true;
   if (a == "--smoke-test") smoke = true;
  }
  if (!smoke && snip::platform::otherInstance(snipNow ? "snip" : tray ? "tray" : "show")) return false;

  SetAppName("Snip");
  SetVendorName("tcottrill");
  SetAppearance(Appearance::System);   // before the first window
  wxImage::AddHandler(new wxPNGHandler);
  g_main = new snip::MainWindow;
  SetTopWindow(g_main);

  if (smoke) {
   wxImage image(301, 151);
   image.SetRGB(wxRect(0, 0, 301, 151), 0x25, 0x63, 0xEB);
   g_main->showImage(image, wxDefaultPosition, false);
   smokeTimer_.Bind(wxEVT_TIMER, [](wxTimerEvent&) { snip::quitApp(); });
   smokeTimer_.StartOnce(700);
   return true;
  }

  snip::platform::startShortcuts(
   [] { if (g_main) g_main->startCapture(snip::CaptureMode::Rectangle); },
   [] { if (g_main) g_main->showMain(); });
  g_tray = new Tray;
  const wxString keys = snip::platform::shortcutNames();
  if (g_tray->SetIcon(wxBitmapBundle::FromIconBundle(snip::appIcons()),
                      keys.empty() ? wxString("Snip - click to snip") : "Snip - click or " + keys)) {
   g_main->setKeepRunning(true);
  }
  if (snipNow) g_main->CallAfter([] { if (g_main) g_main->startCapture(snip::CaptureMode::Rectangle); });
  else if (!tray || !g_tray->IsIconInstalled()) g_main->showMain();
  return true;
 }
 int OnExit() override
 {
  snip::platform::stopShortcuts();
  return wxApp::OnExit();
 }
};

wxIMPLEMENT_APP(Application);
