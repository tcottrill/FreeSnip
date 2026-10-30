// Snip, wxWidgets version: the same window, frozen-screen selection and tray on
// Windows and Linux. Capture, clipboard, global shortcuts and the single instance
// are platform code (platform_windows.cpp; Linux still uses the Qt build).
#pragma once
#include <wx/wx.h>
#include <functional>
#include <memory>
#include <vector>

namespace snip {

enum class CaptureMode { Rectangle, Window, FullDesktop };

// One monitor: where its surface window goes (screen coordinates) and the same
// area in the desktop image. They have the same size on Windows (Per-Monitor V2).
struct Output { wxRect geometry, pixels; };

struct DesktopShot {
 wxImage image;                 // the whole desktop, one pixel per device pixel
 std::vector<Output> outputs;
 std::vector<wxRect> windows;   // application windows, front to back, in image pixels
};

wxMemoryBuffer pngBytes(const wxImage& image);
wxIconBundle appIcons();
void quitApp();

namespace platform {
// True when Snip is already running; the command ("snip", "show" or "tray") went to it.
bool otherInstance(const wxString& command);
void startShortcuts(std::function<void()> snip, std::function<void()> show);
void stopShortcuts();
wxString shortcutNames();   // "PrtScn / Win+Shift+S", empty when none could be registered
bool grabDesktop(DesktopShot& shot, wxString& error);
bool copyImage(wxWindow* owner, const wxImage& image, const wxMemoryBuffer& png);
bool supportsStartup();
bool startupEnabled();
void setStartup(bool on);
void instantWindow(wxTopLevelWindow* window);   // no show/hide animation
void bringToFront(wxTopLevelWindow* window);
}

// The dimmed, frozen desktop you drag on: one borderless window per monitor,
// all sharing one selection in desktop image pixels.
class FrozenDesktop {
public:
 using Selected = std::function<void(wxImage image, wxPoint screen)>;
 FrozenDesktop(DesktopShot shot, bool windowMode, Selected selected, std::function<void()> cancelled);
 ~FrozenDesktop();
 void show();
 void cancel();
private:
 class Surface;
 friend class Surface;
 DesktopShot shot_;
 Selected selected_;
 std::function<void()> cancelled_;
 std::vector<Surface*> surfaces_;
 Surface* captured_ = nullptr;
 bool windowMode_, dragging_ = false, windowPressed_ = false, finished_ = false;
 wxPoint a_, b_;
 wxRect window_;
 wxRect highlight() const;
 wxString hintText() const;
 wxRect windowAt(wxPoint pixel) const;
 wxPoint screenToPixels(wxPoint screen) const;
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
 void showImage(wxImage image, wxPoint from = wxDefaultPosition, bool copy = true);   // from: screen point of the snip
 void showMain();
 void setKeepRunning(bool on) { keepRunning_ = on; }
private:
 wxImage image_;
 wxMemoryBuffer png_;
 wxBitmap scaled_;
 wxSize scaledFor_;
 wxWindow* preview_;
 wxButton *new_, *capture_, *copy_, *save_;
 std::unique_ptr<FrozenDesktop> frozen_;
 wxTimer timer_;
 CaptureMode mode_ = CaptureMode::Rectangle;
 bool pending_ = false, wasShown_ = false, keepRunning_ = false;
 void capture();
 void restore();
 void copyImage();
 void saveImage();
 void paintPreview();
 void captureMenu();
};

}
