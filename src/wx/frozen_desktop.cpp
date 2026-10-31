#include "snip.h"
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include <algorithm>
#include <cmath>

namespace snip {

// One monitor's part of the frozen desktop. Its window is the output's geometry;
// its bitmaps hold the output's image pixels (more of them with display scaling).
class FrozenDesktop::Surface : public wxFrame {
public:
 Surface(FrozenDesktop* owner, const Output& output)
  : wxFrame(nullptr, wxID_ANY, "FreeSnip", output.geometry.GetPosition(), output.geometry.GetSize(),
            wxBORDER_NONE | wxSTAY_ON_TOP | wxFRAME_NO_TASKBAR),
    owner_(owner), out_(output),
    scale_(output.geometry.width > 0 ? double(output.pixels.width) / output.geometry.width : 1.0)
 {
  const wxImage part = owner->shot_.image.GetSubImage(output.pixels);
  wxImage dim = part.Copy();
  unsigned char* d = dim.GetData();
  const size_t n = static_cast<size_t>(dim.GetWidth()) * dim.GetHeight() * 3;
  for (size_t i = 0; i < n; ++i) d[i] = static_cast<unsigned char>(d[i] * 130 / 255);
#ifdef __WXMSW__
  bright_ = wxBitmap(part);
  dim_ = wxBitmap(dim);
  back_ = wxBitmap(part.GetWidth(), part.GetHeight());
  brightDC_.SelectObject(bright_);
  dimDC_.SelectObject(dim_);
  backDC_.SelectObject(back_);
#else
  bright_ = wxBitmap(part, -1, scale_);   // drawn at logical size, sharp on scaled displays
  dim_ = wxBitmap(dim, -1, scale_);
#endif

  canvas_ = new wxWindow(this, wxID_ANY, wxDefaultPosition, output.geometry.GetSize(), wxBORDER_NONE | wxWANTS_CHARS);
  canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
  canvas_->SetCursor(wxCursor(wxCURSOR_CROSS));
  canvas_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
  canvas_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { if (owner_) owner_->press(this, toPixels(e.GetPosition())); });
  canvas_->Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { if (owner_) owner_->move(toPixels(e.GetPosition())); });
  canvas_->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) { if (owner_) owner_->move(toPixels(e.GetPosition())); });
  canvas_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) { if (owner_) owner_->release(toPixels(e.GetPosition())); });
  canvas_->Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent&) { if (owner_) owner_->cancel(); });
  canvas_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) { if (owner_) owner_->key(e.GetKeyCode()); });
  canvas_->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { if (owner_) owner_->captured_ = nullptr; });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { if (owner_ && e.CanVeto()) { e.Veto(); owner_->cancel(); } else e.Skip(); });
 }
 void detach() { owner_ = nullptr; }
 wxWindow* canvas() const { return canvas_; }
 const Output& output() const { return out_; }
 wxPoint toPixels(wxPoint local) const
 {
  return out_.pixels.GetPosition() + wxPoint(int(std::floor(local.x * scale_)), int(std::floor(local.y * scale_)));
 }
 wxRect toLocal(const wxRect& pixels) const
 {
  const double x0 = (pixels.x - out_.pixels.x) / scale_, y0 = (pixels.y - out_.pixels.y) / scale_;
  const double x1 = (pixels.x + pixels.width - out_.pixels.x) / scale_, y1 = (pixels.y + pixels.height - out_.pixels.y) / scale_;
  const int l = int(std::floor(x0)), t = int(std::floor(y0));
  return wxRect(l, t, int(std::ceil(x1)) - l, int(std::ceil(y1)) - t);
 }
 // Where the frozen image is shown: the whole output, at its exact size.
 bool placed() const { return canvas_->GetClientSize() == out_.geometry.GetSize(); }
 void refreshPixels(const wxRect& pixels)
 {
  if (pixels.IsEmpty()) return;
  const int pad = border() + 1;
  wxRect r = toLocal(pixels).Inflate(pad, pad);
  r.Intersect(wxRect(canvas_->GetClientSize()));
  if (!r.IsEmpty()) canvas_->RefreshRect(r, false);
 }
 // The hint box lives in a band along the top; its width changes with the text.
 void refreshHint()
 {
  canvas_->RefreshRect(wxRect(0, 0, canvas_->GetClientSize().x, margin() + canvas_->GetCharHeight() + 2 * FromDIP(8) + 2), false);
 }
#ifdef __WXMSW__
 // The surface covers one monitor exactly at that monitor's DPI; a DPI change
 // must never resize it the way wx resizes normal windows.
 WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wp, WXLPARAM lp) override
 {
  if (msg == WM_DPICHANGED) return 0;
  return wxFrame::MSWWindowProc(msg, wp, lp);
 }
#endif
private:
 FrozenDesktop* owner_;
 Output out_;
 double scale_;
 wxWindow* canvas_;
 wxBitmap bright_, dim_;
#ifdef __WXMSW__
 wxBitmap back_;
 wxMemoryDC brightDC_, dimDC_, backDC_;   // after the bitmaps: released first
#endif

 int border() const { return std::max(1, FromDIP(2)); }
 int margin() const { return FromDIP(16); }

 void decorate(wxDC& dc, const wxRect& sel)
 {
  if (!sel.IsEmpty()) {
   const wxRect local = toLocal(sel);
   const int b = border();
   dc.SetPen(*wxTRANSPARENT_PEN);
   dc.SetBrush(wxBrush(wxColour(0x60, 0xa5, 0xfa)));
   dc.DrawRectangle(local.x - b, local.y - b, local.width + 2 * b, b);
   dc.DrawRectangle(local.x - b, local.GetBottom() + 1, local.width + 2 * b, b);
   dc.DrawRectangle(local.x - b, local.y, b, local.height);
   dc.DrawRectangle(local.GetRight() + 1, local.y, b, local.height);
  }
  const wxString text = owner_->hintText();
  dc.SetFont(canvas_->GetFont());
  const wxSize extent = dc.GetTextExtent(text);
  const wxRect hint(margin(), margin(), extent.x + 2 * FromDIP(12), extent.y + 2 * FromDIP(8));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(wxColour(25, 25, 25)));
  dc.DrawRectangle(hint);
  dc.SetTextForeground(*wxWHITE);
  dc.DrawLabel(text, hint, wxALIGN_CENTER);
 }

 void paint()
 {
  wxPaintDC dc(canvas_);
  if (!owner_) return;
  const wxRect sel = owner_->highlight();
  wxRect lit = sel.IsEmpty() ? wxRect() : toLocal(sel);
#ifdef __WXMSW__
  // GDI draws straight to the screen: compose the changed area off-screen first.
  const wxRect box = canvas_->GetUpdateRegion().GetBox();
  backDC_.Blit(box.GetPosition(), box.GetSize(), &dimDC_, box.GetPosition());
  lit.Intersect(box);
  if (!lit.IsEmpty()) backDC_.Blit(lit.GetPosition(), lit.GetSize(), &brightDC_, lit.GetPosition());
  decorate(backDC_, sel);
  dc.Blit(box.GetPosition(), box.GetSize(), &backDC_, box.GetPosition());
#else
  // GTK paints into its own buffer.
  dc.DrawBitmap(dim_, 0, 0);
  if (!lit.IsEmpty()) {
   dc.SetClippingRegion(lit);
   dc.DrawBitmap(bright_, 0, 0);
   dc.DestroyClippingRegion();
  }
  decorate(dc, sel);
#endif
 }
};

FrozenDesktop::FrozenDesktop(DesktopShot shot, bool windowMode, Selected selected, std::function<void(wxString)> cancelled)
 : shot_(std::move(shot)), selected_(std::move(selected)), cancelled_(std::move(cancelled)), windowMode_(windowMode)
{
}

FrozenDesktop::~FrozenDesktop() { dismiss(); }

void FrozenDesktop::show()
{
 for (const auto& output : shot_.outputs) {
  wxRect inImage = output.pixels;
  inImage.Intersect(wxRect(shot_.image.GetSize()));
  if (inImage != output.pixels || output.geometry.IsEmpty()) continue;   // the layout changed while capturing
  surfaces_.push_back(new Surface(this, output));
 }
 if (surfaces_.empty()) { cancel("The screenshot does not match the monitor layout. Try again."); return; }
 const wxPoint cursor = wxGetMousePosition();
 Surface* active = surfaces_.front();
 for (auto* s : surfaces_) {
  platform::instantWindow(s);
  platform::placeSurface(s, s->output());
  s->Show();
  platform::placeSurface(s, s->output());
  if (s->output().geometry.Contains(cursor)) active = s;
 }
 pointer_ = screenToPixels(cursor);
 if (windowMode_) window_ = windowAt(pointer_);
 active->Raise();
 platform::bringToFront(active);
 active->canvas()->SetFocus();
 redrawAll();
}

void FrozenDesktop::showSelection(const wxRect& pixels)
{
 const wxRect before = highlight();
 fixed_ = pixels;
 changed(before);
}

std::vector<wxTopLevelWindow*> FrozenDesktop::windows() const
{
 return std::vector<wxTopLevelWindow*>(surfaces_.begin(), surfaces_.end());
}

wxRect FrozenDesktop::highlight() const
{
 if (!fixed_.IsEmpty()) return fixed_;
 if (windowMode_) return window_;
 if (!dragging_) return {};
 wxRect r(wxPoint(std::min(a_.x, b_.x), std::min(a_.y, b_.y)), wxPoint(std::max(a_.x, b_.x), std::max(a_.y, b_.y)));
 r.Intersect(wxRect(shot_.image.GetSize()));
 return r;
}

wxString FrozenDesktop::hintText() const
{
 const wxRect r = highlight();
 if (windowMode_) {
  return r.IsEmpty() ? wxString("Click a window    R: rectangle    Enter: this screen    Esc / right-click: cancel")
                     : wxString::Format("%d x %d pixels    Click to snip this window    R: rectangle    Esc: cancel", r.width, r.height);
 }
 if (dragging_ || !fixed_.IsEmpty()) return wxString::Format("%d x %d pixels", r.width, r.height);
 // Window picking needs the window list, which only Windows provides.
 return shot_.windows.empty() ? "Drag to snip    Enter: this screen    Esc / right-click: cancel"
                              : "Drag to snip    W: window    Enter: this screen    Esc / right-click: cancel";
}

wxRect FrozenDesktop::windowAt(wxPoint pixel) const
{
 for (const auto& r : shot_.windows) if (r.Contains(pixel)) return r;
 return {};
}

// Exact on Windows. Wayland gives no global pointer position; events track it there.
wxPoint FrozenDesktop::screenToPixels(wxPoint screen) const
{
 for (const auto& o : shot_.outputs) {
  if (!o.geometry.Contains(screen)) continue;
  const double s = double(o.pixels.width) / o.geometry.width;
  return o.pixels.GetPosition() + wxPoint(int((screen.x - o.geometry.x) * s), int((screen.y - o.geometry.y) * s));
 }
 return wxPoint(-1, -1);
}

// A desktop that put a surface somewhere else, or at another size, would make
// the selection miss what is shown: refuse rather than copy the wrong pixels.
bool FrozenDesktop::placed() const
{
 for (auto* s : surfaces_) if (!s->placed()) return false;
 return true;
}

void FrozenDesktop::press(Surface* surface, wxPoint pixel)
{
 if (finished_) return;
 if (!placed()) {
  cancel("The desktop did not place the frozen screen exactly over each monitor, so the snip was cancelled "
         "to avoid a misaligned result. Use Capture > Desktop options, or retry.");
  return;
 }
 pointer_ = pixel;
 const wxRect before = highlight();
 if (windowMode_) {
  window_ = windowAt(pixel);
  windowPressed_ = !window_.IsEmpty();
 } else {
  dragging_ = true;
  a_ = b_ = pixel;
 }
 if (dragging_ || windowPressed_) {
  surface->canvas()->CaptureMouse();
  captured_ = surface;
 }
 changed(before);
}

void FrozenDesktop::move(wxPoint pixel)
{
 if (finished_) return;
 pointer_ = pixel;
 const wxRect before = highlight();
 if (windowMode_) window_ = windowAt(pixel);
 else if (dragging_) b_ = pixel;
 else return;
 if (highlight() != before) changed(before);
}

void FrozenDesktop::release(wxPoint pixel)
{
 if (finished_) return;
 releaseMouse();
 pointer_ = pixel;
 if (windowMode_) {
  const bool pressed = windowPressed_;
  windowPressed_ = false;
  window_ = windowAt(pixel);
  if (pressed && !window_.IsEmpty()) finish(window_);
  return;
 }
 if (!dragging_) return;
 const wxRect before = highlight();
 b_ = pixel;
 const wxRect r = highlight();
 dragging_ = false;
 if (r.width > 1 && r.height > 1) finish(r);
 else changed(before);   // a click, not a drag
}

void FrozenDesktop::key(int code)
{
 if (finished_) return;
 if (code == WXK_ESCAPE) {
  cancel();
 } else if (code == WXK_RETURN || code == WXK_NUMPAD_ENTER) {
  if (!placed()) { press(nullptr, pointer_); return; }   // reports the placement problem
  for (const auto& o : shot_.outputs) if (o.pixels.Contains(pointer_)) { finish(o.pixels); return; }
  for (auto* s : surfaces_) if (s->canvas()->HasFocus()) { finish(s->output().pixels); return; }
  finish(shot_.outputs.front().pixels);
 } else if ((code == 'W' && !shot_.windows.empty()) || code == 'R') {
  releaseMouse();
  dragging_ = windowPressed_ = false;
  windowMode_ = code == 'W';
  window_ = windowMode_ ? windowAt(pointer_) : wxRect();
  redrawAll();
 }
}

void FrozenDesktop::releaseMouse()
{
 if (captured_ && captured_->canvas()->HasCapture()) captured_->canvas()->ReleaseMouse();
 captured_ = nullptr;
}

void FrozenDesktop::changed(const wxRect& before)
{
 const wxRect after = highlight();
 for (auto* s : surfaces_) {
  s->refreshPixels(before);
  s->refreshPixels(after);
  s->refreshHint();
 }
}

void FrozenDesktop::redrawAll()
{
 for (auto* s : surfaces_) s->canvas()->Refresh(false);
}

void FrozenDesktop::cancel(const wxString& error)
{
 if (finished_) return;
 finished_ = true;
 dismiss();
 auto done = cancelled_;
 wxTheApp->CallAfter([done, error] { if (done) done(error); });
}

void FrozenDesktop::finish(wxRect r)
{
 r.Intersect(wxRect(shot_.image.GetSize()));
 if (finished_ || r.IsEmpty()) return;
 finished_ = true;
 wxPoint screen = wxDefaultPosition;
 const wxPoint centre(r.x + r.width / 2, r.y + r.height / 2);
 for (const auto& o : shot_.outputs) {
  if (!o.pixels.Contains(centre)) continue;
  const double s = double(o.geometry.width) / o.pixels.width;
  screen = o.geometry.GetPosition() + wxPoint(int((centre.x - o.pixels.x) * s), int((centre.y - o.pixels.y) * s));
 }
 wxImage image = shot_.image.GetSubImage(r);
 dismiss();
 auto done = selected_;
 wxTheApp->CallAfter([done, image, screen] { if (done) done(image, screen); });
}

// Called from the surfaces' own event handlers: Destroy() deletes them later.
void FrozenDesktop::dismiss()
{
 releaseMouse();
 for (auto* s : surfaces_) {
  s->detach();
  s->Hide();
  s->Destroy();
 }
 surfaces_.clear();
}

}
