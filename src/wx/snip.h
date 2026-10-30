// Snip: the same window, frozen-screen selection and tray on Windows and Linux,
// in wxWidgets. Capture, clipboard, global shortcuts, the tray icon and the
// single instance are platform code (platform_windows.cpp, platform_linux.cpp).
#pragma once
#include <wx/wx.h>
#include <functional>
#include <memory>
#include <vector>

namespace snip {

// ActiveWindow and DesktopOptions go through the Linux screenshot portal.
enum class CaptureMode { Rectangle, Window, FullDesktop, ActiveWindow, DesktopOptions };

// One monitor: where its surface window goes (screen coordinates) and the same
// area in the desktop image. Equal sizes on Windows; on Linux the image can have
// more pixels than the logical geometry (display scaling).
struct Output { wxRect geometry, pixels; int monitor = -1; };

struct DesktopShot {
 wxImage image;                 // the whole desktop, one pixel per device pixel
 std::vector<Output> outputs;
 std::vector<wxRect> windows;   // application windows, front to back, in image pixels (Windows only)
};

// A tray menu entry: an empty label is a separator.
struct TrayItem {
 TrayItem(int id = 0, const wxString& label = {}, bool checkable = false, bool checked = false)
  : id(id), label(label), checkable(checkable), checked(checked) {}
 int id;
 wxString label;
 bool checkable, checked;
 std::vector<TrayItem> submenu;
};

struct TrayActions {
 std::function<void()> click;
 std::function<std::vector<TrayItem>()> menu;
 std::function<void(int)> chosen;
};

wxMemoryBuffer pngBytes(const wxImage& image);
wxImage appIconImage(int size);
wxIconBundle appIcons();
void quitApp();

namespace platform {
// True when Snip is already running; the command ("snip", "show" or "tray") went to it.
bool otherInstance(const wxString& command);
void startShortcuts(std::function<void()> snip, std::function<void()> show);
void stopShortcuts();
wxString shortcutNames();   // "PrtScn / Win+Shift+S", empty when none are registered
// Linux: ask the GlobalShortcuts portal for a capture key; progress goes to status.
bool canSetupShortcut();
void setupShortcut(std::function<void(const wxString&)> status);
// Reads the desktop, now or (Linux portal) later. done gets an invalid image and an
// empty error when the user cancelled. Never called after cancelCapture().
void capture(CaptureMode mode, std::function<void(DesktopShot shot, wxString error)> done);
void cancelCapture();
bool copyImage(wxWindow* owner, const wxImage& image, const wxMemoryBuffer& png);
bool startTray(TrayActions actions, const wxString& tooltip);
void stopTray();
bool supportsStartup();
bool startupEnabled();
void setStartup(bool on);
void placeSurface(wxTopLevelWindow* window, const Output& output);
void instantWindow(wxTopLevelWindow* window);   // no show/hide animation
void bringToFront(wxTopLevelWindow* window);
}

// The dimmed, frozen desktop you drag on: one borderless window per monitor,
// all sharing one selection in desktop image pixels.
class FrozenDesktop {
public:
 using Selected = std::function<void(wxImage image, wxPoint screen)>;
 // cancelled gets an error message when the surfaces could not be placed correctly.
 FrozenDesktop(DesktopShot shot, bool windowMode, Selected selected, std::function<void(wxString)> cancelled);
 ~FrozenDesktop();
 void show();
 void cancel(const wxString& error = {});
 void showSelection(const wxRect& pixels);   // as if dragged; for smoke-test screenshots
 std::vector<wxTopLevelWindow*> windows() const;
private:
 class Surface;
 friend class Surface;
 DesktopShot shot_;
 Selected selected_;
 std::function<void(wxString)> cancelled_;
 std::vector<Surface*> surfaces_;
 Surface* captured_ = nullptr;
 bool windowMode_, dragging_ = false, windowPressed_ = false, finished_ = false;
 wxPoint a_, b_, pointer_{ -1, -1 };
 wxRect window_, fixed_;
 wxRect highlight() const;
 wxString hintText() const;
 wxRect windowAt(wxPoint pixel) const;
 wxPoint screenToPixels(wxPoint screen) const;
 bool placed() const;
 void press(Surface* surface, wxPoint pixel);
 void move(wxPoint pixel);
 void release(wxPoint pixel);
 void key(int code);
 void releaseMouse();
 void changed(const wxRect& before);
 void redrawAll();
 void finish(wxRect pixels);
 void dismiss();
};

class MainWindow : public wxFrame {
public:
 MainWindow();
 ~MainWindow() override;
 void startCapture(CaptureMode mode, int delayMs = 0);
 void cancelCapture();
 void showImage(wxImage image, wxPoint from = wxDefaultPosition, bool copy = true);   // from: screen point of the snip
 void showMain();
 void setKeepRunning(bool on) { keepRunning_ = on; }
 void setInert() { inert_ = true; }   // smoke tests: keys and clicks never start a capture
private:
 wxImage image_;
 wxMemoryBuffer png_;
 wxBitmap scaled_;
 wxSize scaledFor_;
 wxWindow* preview_;
 wxButton *new_, *capture_, *copy_, *save_, *cancel_ = nullptr;
 std::unique_ptr<FrozenDesktop> frozen_;
 wxTimer timer_;
 CaptureMode mode_ = CaptureMode::Rectangle;
 bool pending_ = false, wasShown_ = false, keepRunning_ = false, inert_ = false;
 unsigned generation_ = 0;
 void capture();
 void captured(DesktopShot shot, const wxString& error);
 void restore();
 void copyImage();
 void saveImage();
 void paintPreview();
 void captureMenu();
};

}
