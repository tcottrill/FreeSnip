#include "snip.h"
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include <algorithm>

namespace snip {

// One monitor's part of the frozen desktop. Pixels map 1:1 to the window, so
// local coordinates are the output's image pixels minus its origin.
class FrozenDesktop::Surface : public wxFrame {
public:
 Surface(FrozenDesktop* owner, const Output& output)
  : wxFrame(nullptr, wxID_ANY, "Snip", output.geometry.GetPosition(), output.geometry.GetSize(),
            wxBORDER_NONE | wxSTAY_ON_TOP | wxFRAME_NO_TASKBAR),
    owner_(owner), out_(output)
 {
  const wxImage part = owner->shot_.image.GetSubImage(output.pixels);
  wxImage dim = part.Copy();
  unsigned char* d = dim.GetData();
  const size_t n = static_cast<size_t>(dim.GetWidth()) * dim.GetHeight() * 3;
  for (size_t i = 0; i < n; ++i) d[i] = static_cast<unsigned char>(d[i] * 130 / 255);
  bright_ = wxBitmap(part);
  dim_ = wxBitmap(dim);
  back_ = wxBitmap(part.GetWidth(), part.GetHeight());
  brightDC_.SelectObject(bright_);
  dimDC_.SelectObject(dim_);
  backDC_.SelectObject(back_);

  canvas_ = new wxWindow(this, wxID_ANY, wxDefaultPosition, output.geometry.GetSize(), wxBORDER_NONE | wxWANTS_CHARS);
  canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
  canvas_->SetCursor(wxCursor(wxCURSOR_CROSS));
  canvas_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
  canvas_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { if (owner_) owner_->press(this, toPixels(e.GetPosition())); });
  canvas_->Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { if (owner_) owner_->move(toPixels(e.GetPosition())); });
  canvas_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) { if (owner_) owner_->release(toPixels(e.GetPosition())); });
  canvas_->Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent&) { if (owner_) owner_->cancel(); });
  canvas_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) { if (owner_) owner_->key(e.GetKeyCode()); });
  canvas_->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { if (owner_) owner_->captured_ = nullptr; });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { if (owner_ && e.CanVeto()) { e.Veto(); owner_->cancel(); } else e.Skip(); });
 }
 void detach() { owner_ = nullptr; }
 wxWindow* canvas() const { return canvas_; }
 const Output& output() const { return out_; }
 wxPoint toPixels(wxPoint local) const { return local + out_.pixels.GetPosition(); }
 wxRect toLocal(wxRect pixels) const { pixels.Offset(-out_.pixels.GetPosition()); return pixels; }
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
 void place()
 {
  if (GetRect() != out_.geometry) SetSize(out_.geometry);
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
 wxWindow* canvas_;
 wxBitmap bright_, dim_, back_;
 wxMemoryDC brightDC_, dimDC_, backDC_;   // after the bitmaps: released first

 int border() const { return std::max(1, FromDIP(2)); }
 int margin() const { return FromDIP(16); }

 void paint()
 {
  wxPaintDC dc(canvas_);
  if (!owner_) return;
  const wxRect box = canvas_->GetUpdateRegion().GetBox();
  backDC_.Blit(box.GetPosition(), box.GetSize(), &dimDC_, box.GetPosition());
  const wxRect sel = owner_->highlight();
  if (!sel.IsEmpty()) {
   const wxRect local = toLocal(sel);
   wxRect lit = local;
   lit.Intersect(box);
   if (!lit.IsEmpty()) backDC_.Blit(lit.GetPosition(), lit.GetSize(), &brightDC_, lit.GetPosition());
   const int b = border();
   backDC_.SetPen(*wxTRANSPARENT_PEN);
   backDC_.SetBrush(wxBrush(wxColour(0x60, 0xa5, 0xfa)));
   backDC_.DrawRectangle(local.x - b, local.y - b, local.width + 2 * b, b);
   backDC_.DrawRectangle(local.x - b, local.GetBottom() + 1, local.width + 2 * b, b);
   backDC_.DrawRectangle(local.x - b, local.y, b, local.height);
   backDC_.DrawRectangle(local.GetRight() + 1, local.y, b, local.height);
  }
  const wxString text = owner_->hintText();
  backDC_.SetFont(canvas_->GetFont());
  const wxSize extent = backDC_.GetTextExtent(text);
  const wxRect hint(margin(), margin(), extent.x + 2 * FromDIP(12), extent.y + 2 * FromDIP(8));
  backDC_.SetPen(*wxTRANSPARENT_PEN);
  backDC_.SetBrush(wxBrush(wxColour(25, 25, 25)));
  backDC_.DrawRectangle(hint);
  backDC_.SetTextForeground(*wxWHITE);
  backDC_.DrawLabel(text, hint, wxALIGN_CENTER);
  dc.Blit(box.GetPosition(), box.GetSize(), &backDC_, box.GetPosition());
 }
};

FrozenDesktop::FrozenDesktop(DesktopShot shot, bool windowMode, Selected selected, std::function<void()> cancelled)
 : shot_(std::move(shot)), selected_(std::move(selected)), cancelled_(std::move(cancelled)), windowMode_(windowMode)
{
}

FrozenDesktop::~FrozenDesktop() { dismiss(); }

void FrozenDesktop::show()
{
 for (const auto& output : shot_.outputs) {
  wxRect inImage = output.pixels;
  inImage.Intersect(wxRect(shot_.image.GetSize()));
  if (inImage != output.pixels) continue;   // the monitor layout changed while capturing
  surfaces_.push_back(new Surface(this, output));
 }
 if (surfaces_.empty()) { cancel(); return; }
 const wxPoint cursor = wxGetMousePosition();
 Surface* active = surfaces_.front();
 for (auto* s : surfaces_) {
  platform::instantWindow(s);
  s->Show();
  s->place();
  if (s->output().geometry.Contains(cursor)) active = s;
 }
 if (windowMode_) window_ = windowAt(screenToPixels(cursor));
 active->Raise();
 platform::bringToFront(active);
 active->canvas()->SetFocus();
 redrawAll();
}

wxRect FrozenDesktop::highlight() const
{
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
 if (dragging_) return wxString::Format("%d x %d pixels", r.width, r.height);
 return "Drag to snip    W: window    Enter: this screen    Esc / right-click: cancel";
}

wxRect FrozenDesktop::windowAt(wxPoint pixel) const
{
 for (const auto& r : shot_.windows) if (r.Contains(pixel)) return r;
 return {};
}

wxPoint FrozenDesktop::screenToPixels(wxPoint screen) const
{
 for (const auto& o : shot_.outputs)
  if (o.geometry.Contains(screen)) return screen - o.geometry.GetPosition() + o.pixels.GetPosition();
 return wxPoint(-1, -1);
}

void FrozenDesktop::press(Surface* surface, wxPoint pixel)
{
 if (finished_) return;
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
  const wxPoint cursor = wxGetMousePosition();
  for (const auto& o : shot_.outputs) if (o.geometry.Contains(cursor)) { finish(o.pixels); return; }
 } else if (code == 'W' || code == 'R') {
  releaseMouse();
  dragging_ = windowPressed_ = false;
  windowMode_ = code == 'W';
  window_ = windowMode_ ? windowAt(screenToPixels(wxGetMousePosition())) : wxRect();
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

void FrozenDesktop::cancel()
{
 if (finished_) return;
 finished_ = true;
 dismiss();
 auto done = cancelled_;
 wxTheApp->CallAfter([done] { if (done) done(); });
}

void FrozenDesktop::finish(wxRect r)
{
 r.Intersect(wxRect(shot_.image.GetSize()));
 if (finished_ || r.IsEmpty()) return;
 finished_ = true;
 wxPoint screen = wxDefaultPosition;
 const wxPoint centre(r.x + r.width / 2, r.y + r.height / 2);
 for (const auto& o : shot_.outputs)
  if (o.pixels.Contains(centre)) screen = centre - o.pixels.GetPosition() + o.geometry.GetPosition();
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
