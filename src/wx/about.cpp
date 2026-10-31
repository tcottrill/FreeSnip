// The About box with its theme choice, and the Ko-fi button, both as in Skein
// Downloader (C:\Source2026\Downloader-lite, src/wx/app.cpp and manager.cpp).
#include "snip.h"
#include <wx/aboutdlg.h>
#include <wx/generic/aboutdlgg.h>
#include <wx/config.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/mstream.h>
#include "kofi_cup.h"
#include <algorithm>
#include <cmath>

namespace snip {
namespace {

const char* const kofiPage = "https://ko-fi.com/timcottrill";

// The "theme" setting: "system" (the default), "light" or "dark", chosen in the About box.
const char* const themeNames[] = { "system", "light", "dark" };

wxApp::Appearance appearance(const wxString& theme)
{
 return theme == "light" ? wxApp::Appearance::Light : theme == "dark" ? wxApp::Appearance::Dark : wxApp::Appearance::System;
}

wxString savedTheme() { return wxConfigBase::Get()->Read("theme", "system"); }

// wx's own About box with a theme choice under the text. GTK changes the theme of open windows
// at once; Windows (wx's dark mode) only when windows are created, so there it applies on the
// next start.
class AboutDialog : public wxGenericAboutDialog {
 void DoAddCustomControls() override
 {
  auto row = new wxPanel(this);
  auto box = new wxBoxSizer(wxHORIZONTAL);
  box->Add(new wxStaticText(row, wxID_ANY, "Theme"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
  auto choice = new wxChoice(row, wxID_ANY);
  for (auto s : { "Same as system", "Light", "Dark" }) choice->Append(s);
  const wxString saved = savedTheme();
  choice->SetSelection(0);
  for (int i = 0; i < 3; ++i) if (saved == themeNames[i]) choice->SetSelection(i);
  box->Add(choice, 0, wxALIGN_CENTER_VERTICAL);
  row->SetSizer(box);
  AddControl(row, wxSizerFlags().Border(wxUP | wxDOWN));
  choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) {
   const wxString theme = themeNames[std::clamp(e.GetSelection(), 0, 2)];
   wxConfigBase::Get()->Write("theme", theme);
   wxConfigBase::Get()->Flush();
#ifdef __WXMSW__
   wxMessageBox("The new theme applies the next time FreeSnip starts.", "Theme", wxOK | wxICON_INFORMATION, this);
#else
   wxTheApp->SetAppearance(appearance(theme));
#endif
  });
 }
};

// Ko-fi's "Support me on Ko-fi" button, drawn at the exact pixel size of each monitor: the
// label in the system font and the rounded outline as anti-aliased shapes, so only the cup
// logo is an image (scaled down with high-quality filtering). A scaled picture of the whole
// badge blurs its small text. Light or dark following the toolbar, like Ko-fi's two badges.
class KofiButton : public wxControl {
 bool hover = false, pressed = false;
 wxImage cup;
 wxBitmap cupBitmap;
 int cupHeight = 0;
 wxFont labelFont() const { return GetFont().Bold(); }
 int height() const { return FromDIP(30); }
 bool dark() const { return GetParent()->GetBackgroundColour().GetLuminance() < 0.5; }
 void paint()
 {
  wxAutoBufferedPaintDC dc(this);
  dc.SetBackground(GetParent()->GetBackgroundColour());
  dc.Clear();
  const auto r = GetClientRect();
  const int h = r.height;
  const bool isDark = dark();
  // Ko-fi's colours: cream with a near-black outline, or near-black with white text.
  wxColour fill = isDark ? wxColour(32, 32, 32) : wxColour(244, 239, 231), edge = isDark ? wxColour(32, 32, 32) : wxColour(41, 41, 41);
  if (pressed) fill = fill.ChangeLightness(isDark ? 115 : 92);
  else if (hover) fill = fill.ChangeLightness(isDark ? 125 : 103);
  if (isDark && (hover || pressed)) edge = fill;
  {
   std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
   const double line = std::max(1.0, h / 15.0);
   gc->SetPen(wxPen(edge, int(std::lround(line))));
   gc->SetBrush(fill);
   gc->DrawRoundedRectangle(line / 2, line / 2, r.width - line, h - line, h * 0.3);
  }
  const int ch = int(std::lround(h * 0.58));
  if (ch != cupHeight && cup.IsOk()) {
   cupBitmap = wxBitmap(cup.Scale(int(std::lround(cup.GetWidth() * double(ch) / cup.GetHeight())), ch, wxIMAGE_QUALITY_HIGH));
   cupHeight = ch;
  }
  const int x = int(std::lround(h * 0.3));
  if (cupBitmap.IsOk()) dc.DrawBitmap(cupBitmap, x, (h - cupBitmap.GetHeight()) / 2, true);
  dc.SetFont(labelFont());
  dc.SetTextForeground(isDark ? *wxWHITE : wxColour(32, 32, 32));
  const auto text = dc.GetTextExtent(GetLabel());
  dc.DrawText(GetLabel(), x + (cupBitmap.IsOk() ? cupBitmap.GetWidth() : 0) + int(std::lround(h * 0.22)), (h - text.y) / 2);
 }
public:
 explicit KofiButton(wxWindow* parent) : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
 {
  if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) wxImage::AddHandler(new wxPNGHandler);
  wxMemoryInputStream in(kofiCup, sizeof kofiCup);
  cup.LoadFile(in, wxBITMAP_TYPE_PNG);
  SetLabel("Support me on Ko-fi");
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetCursor(wxCursor(wxCURSOR_HAND));
  Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
  Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hover = true; Refresh(); });
  Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover = pressed = false; Refresh(); });
  Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { pressed = true; Refresh(); });
  Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
   const bool click = pressed && hover;
   pressed = false;
   Refresh();
   if (click) {
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
   }
  });
  // A new monitor scale changes the font and the size; the cup is rescaled on the next paint.
  Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& e) { e.Skip(); InvalidateBestSize(); Refresh(); });
  // A theme change (GTK applies it live) changes the toolbar background.
  Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& e) { e.Skip(); Refresh(); });
 }
 bool AcceptsFocus() const override { return false; }
protected:
 wxSize DoGetBestClientSize() const override
 {
  int w = 0, h = 0;
  const int H = height();
  const wxFont font = labelFont();
  GetTextExtent(GetLabel(), &w, &h, nullptr, nullptr, &font);
  const int cupWidth = cup.IsOk() ? int(std::lround(cup.GetWidth() * H * 0.58 / cup.GetHeight())) : 0;
  return wxSize(int(std::lround(H * 0.3)) + cupWidth + int(std::lround(H * 0.22)) + w + int(std::lround(H * 0.45)), H);
 }
};

} // namespace

wxApp::Appearance savedAppearance() { return appearance(savedTheme()); }

wxWindow* makeKofiButton(wxWindow* parent)
{
 auto kofi = new KofiButton(parent);
 kofi->SetToolTip("Support FreeSnip on Ko-fi (opens ko-fi.com/timcottrill in your browser)");
 kofi->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { wxLaunchDefaultBrowser(kofiPage); });
 return kofi;
}

void showAbout(wxWindow* parent)
{
 wxAboutDialogInfo info;
 info.SetName("FreeSnip");
 info.SetVersion(SNIP_VERSION);
 info.SetDescription("Grab it. Clip it. Done.\n\n"
                     "A small screen snipping tool: drag a rectangle on the frozen screen,\n"
                     "then paste it anywhere or save it as a PNG.");
 info.SetCopyright(wxString::FromUTF8("Copyright \xC2\xA9 2026 Tim Cottrill"));
 wxIcon icon;
 icon.CopyFromBitmap(wxBitmap(appIconImage(parent->FromDIP(64))));
 info.SetIcon(icon);
 AboutDialog dialog;
 if (dialog.Create(info, parent)) dialog.ShowModal();
}

}
