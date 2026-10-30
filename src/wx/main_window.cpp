#include "snip.h"
#include <wx/config.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/datetime.h>
#include <wx/wfstream.h>
#include <wx/file.h>
#include <algorithm>

namespace snip {
namespace {
enum { ID_NEW = wxID_HIGHEST + 1, ID_RECTANGLE, ID_WINDOW, ID_DESKTOP, ID_COPY, ID_SAVE, ID_CLOSE };
}

MainWindow::MainWindow() : wxFrame(nullptr, wxID_ANY, "Snip")
{
 SetIcons(appIcons());
 auto panel = new wxPanel(this);
 auto column = new wxBoxSizer(wxVERTICAL);
 auto bar = new wxBoxSizer(wxHORIZONTAL);
 auto add = [&](const wxString& label, int id) {
  auto b = new wxButton(panel, id, label);
  bar->Add(b, 0, bar->GetItemCount() ? wxLEFT : 0, FromDIP(6));
  return b;
 };
 new_ = add("New", ID_NEW);
 capture_ = add(wxString::FromUTF8("Capture \xE2\x96\xBE"), wxID_ANY);
 copy_ = add("Copy", ID_COPY);
 save_ = add("Save As...", ID_SAVE);
 add("Quit", wxID_EXIT);
 copy_->Disable();
 save_->Disable();
 column->Add(bar, 0, wxALL, FromDIP(6));
 preview_ = new wxWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
 preview_->SetBackgroundStyle(wxBG_STYLE_PAINT);
 preview_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paintPreview(); });
 column->Add(preview_, 1, wxEXPAND);
 panel->SetSizer(column);
 CreateStatusBar();
 SetStatusText("Ready");

 auto on = [this](int id, std::function<void()> action) {
  Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); }, id);
  Bind(wxEVT_MENU, [action](wxCommandEvent&) { action(); }, id);
 };
 on(ID_NEW, [this] { startCapture(CaptureMode::Rectangle); });
 on(ID_RECTANGLE, [this] { startCapture(CaptureMode::Rectangle); });
 on(ID_WINDOW, [this] { startCapture(CaptureMode::Window); });
 on(ID_DESKTOP, [this] { startCapture(CaptureMode::FullDesktop); });
 on(ID_COPY, [this] { copyImage(); });
 on(ID_SAVE, [this] { saveImage(); });
 on(ID_CLOSE, [this] { Close(); });
 on(wxID_EXIT, [] { quitApp(); });
 capture_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { captureMenu(); });

 wxAcceleratorEntry keys[] = {
  { wxACCEL_CTRL, 'N', ID_NEW },
  { wxACCEL_CTRL | wxACCEL_SHIFT, 'W', ID_WINDOW },
  { wxACCEL_CTRL, 'C', ID_COPY },
  { wxACCEL_CTRL, 'S', ID_SAVE },
  { wxACCEL_CTRL, 'Q', wxID_EXIT },
  { wxACCEL_NORMAL, WXK_ESCAPE, ID_CLOSE },
 };
 SetAcceleratorTable(wxAcceleratorTable(static_cast<int>(std::size(keys)), keys));

 Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
  if (keepRunning_ && e.CanVeto()) { e.Veto(); Hide(); return; }   // keep running in the tray
  quitApp();
 });
 timer_.SetOwner(this);
 Bind(wxEVT_TIMER, [this](wxTimerEvent&) { capture(); }, timer_.GetId());

 SetMinSize(FromDIP(wxSize(420, 240)));
 SetSize(FromDIP(wxSize(760, 500)));
 CentreOnScreen();
 platform::instantWindow(this);
}

MainWindow::~MainWindow()
{
 timer_.Stop();
 frozen_.reset();
}

void MainWindow::captureMenu()
{
 wxMenu menu;
 menu.Append(ID_RECTANGLE, "Rectangle\tCtrl+N");
 menu.Append(ID_WINDOW, "Window\tCtrl+Shift+W");
 menu.Append(ID_DESKTOP, "Full desktop");
 PopupMenu(&menu, capture_->GetPosition() + wxPoint(0, capture_->GetSize().y));
}

void MainWindow::startCapture(CaptureMode mode, int delayMs)
{
 if (pending_ || frozen_) return;
 mode_ = mode;
 pending_ = true;
 wasShown_ = IsShown() && !IsIconized();
 new_->Disable();
 capture_->Disable();
 if (IsShown()) {
  Hide();
  delayMs = std::max(delayMs, 200);   // let the window leave the screen before it is read
 }
 if (delayMs > 0) timer_.StartOnce(delayMs);
 else capture();
}

void MainWindow::capture()
{
 DesktopShot shot;
 wxString error;
 if (!platform::grabDesktop(shot, error)) {
  restore();
  wxMessageBox(error, "Screen capture", wxOK | wxICON_WARNING, this);
  return;
 }
 if (mode_ == CaptureMode::FullDesktop) {
  showImage(std::move(shot.image), wxGetMousePosition());
  return;
 }
 frozen_ = std::make_unique<FrozenDesktop>(std::move(shot), mode_ == CaptureMode::Window,
  [this](wxImage image, wxPoint screen) { frozen_.reset(); showImage(std::move(image), screen); },
  [this] { frozen_.reset(); restore(); });
 frozen_->show();
}

void MainWindow::restore()
{
 pending_ = false;
 new_->Enable();
 capture_->Enable();
 SetStatusText(image_.IsOk() ? "Last snip kept" : "Ready");
 if (wasShown_) showMain();
}

void MainWindow::showImage(wxImage image, wxPoint from, bool copy)
{
 if (!image.IsOk()) return;
 image_ = std::move(image);
 png_ = pngBytes(image_);
 scaled_ = wxBitmap();
 pending_ = false;
 new_->Enable();
 capture_->Enable();
 copy_->Enable();
 save_->Enable(!png_.IsEmpty());
 SetTitle(wxString::Format("Snip - %d x %d", image_.GetWidth(), image_.GetHeight()));
 if (copy) copyImage();
 // Open on the monitor the snip came from.
 if (!IsShown() && from != wxDefaultPosition) {
  const int display = wxDisplay::GetFromPoint(from);
  if (display != wxNOT_FOUND) {
   const wxRect area = wxDisplay(static_cast<unsigned>(display)).GetClientArea();
   for (int pass = 0; pass < 2; ++pass) {   // again after a DPI change resized the window
    const wxSize size = GetSize();
    SetPosition(wxPoint(area.x + std::max(0, (area.width - size.x) / 2), area.y + std::max(0, (area.height - size.y) / 2)));
   }
  }
 }
 preview_->Refresh();
 showMain();
}

void MainWindow::showMain()
{
 if (pending_ || frozen_) return;
 if (IsIconized()) Iconize(false);
 Show();
 Raise();
 platform::bringToFront(this);
}

void MainWindow::copyImage()
{
 if (!image_.IsOk()) return;
 SetStatusText(platform::copyImage(this, image_, png_) ? "Copied to clipboard - paste into your app"
                                                        : "Clipboard busy, press Copy");
}

void MainWindow::saveImage()
{
 if (png_.IsEmpty()) return;
 wxConfigBase* settings = wxConfigBase::Get();
 const wxString dir = settings->Read("saveDirectory", wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Pictures));
 wxFileDialog dialog(this, "Save snip", dir, "Snip-" + wxDateTime::Now().Format("%Y%m%d-%H%M%S") + ".png",
                     "PNG images (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
 if (dialog.ShowModal() != wxID_OK) return;
 const wxString path = dialog.GetPath();
 wxTempFile file(path);
 if (!file.IsOpened() || !file.Write(png_.GetData(), png_.GetDataLen()) || !file.Commit()) {
  wxMessageBox("Could not save the PNG: " + path, "Save failed", wxOK | wxICON_WARNING, this);
  return;
 }
 settings->Write("saveDirectory", wxFileName(path).GetPath());
 SetStatusText("Saved " + path);
}

void MainWindow::paintPreview()
{
 wxAutoBufferedPaintDC dc(preview_);
 dc.SetBackground(wxBrush(preview_->GetBackgroundColour()));
 dc.Clear();
 const wxSize area = preview_->GetClientSize();
 if (!image_.IsOk()) {
  wxString hint = "New snip captures your screen.\nDrag a rectangle, then paste or save the result.";
  const wxString keys = platform::shortcutNames();
  if (!keys.empty()) hint += "\n\nShortcut: " + keys;
  dc.SetFont(preview_->GetFont());
  dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
  dc.DrawLabel(hint, wxRect(area), wxALIGN_CENTER);
  return;
 }
 const int pad = FromDIP(8);
 const int aw = area.x - 2 * pad, ah = area.y - 2 * pad;
 if (aw <= 0 || ah <= 0) return;
 if (!scaled_.IsOk() || scaledFor_ != area) {
  const double s = std::min({ 1.0, double(aw) / image_.GetWidth(), double(ah) / image_.GetHeight() });
  const int w = std::max(1, int(image_.GetWidth() * s)), h = std::max(1, int(image_.GetHeight() * s));
  scaled_ = wxBitmap(s < 1.0 ? image_.Scale(w, h, wxIMAGE_QUALITY_HIGH) : image_);
  scaledFor_ = area;
 }
 const int x = (area.x - scaled_.GetWidth()) / 2, y = (area.y - scaled_.GetHeight()) / 2;
 dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
 dc.SetBrush(*wxTRANSPARENT_BRUSH);
 dc.DrawRectangle(x - 1, y - 1, scaled_.GetWidth() + 2, scaled_.GetHeight() + 2);
 dc.DrawBitmap(scaled_, x, y);
}

}
