#include "snip.h"
#include <wx/mstream.h>
#include <algorithm>

namespace snip {

wxMemoryBuffer pngBytes(const wxImage& image)
{
 wxMemoryBuffer bytes;
 wxMemoryOutputStream out;
 if (!image.IsOk() || !image.SaveFile(out, wxBITMAP_TYPE_PNG)) return bytes;
 const size_t size = out.GetSize();
 out.CopyTo(bytes.GetWriteBuf(size), size);
 bytes.UngetWriteBuf(size);
 return bytes;
}

// Blue rounded square with white crop corners.
wxImage appIconImage(int s)
{
 wxImage image(s, s);
 image.InitAlpha();
 const float r = s / 5.0f;
 for (int y = 0; y < s; ++y) {
  for (int x = 0; x < s; ++x) {
   const float fx = x + 0.5f, fy = y + 0.5f;
   const float dx = std::max({ r - fx, fx - (s - r), 0.0f });
   const float dy = std::max({ r - fy, fy - (s - r), 0.0f });
   image.SetRGB(x, y, 0x25, 0x63, 0xEB);
   image.SetAlpha(x, y, dx * dx + dy * dy <= r * r ? 255 : 0);
  }
 }
 auto fill = [&](int x0, int y0, int w, int h) {
  for (int y = std::max(y0, 0); y < std::min(y0 + h, s); ++y)
   for (int x = std::max(x0, 0); x < std::min(x0 + w, s); ++x) image.SetRGB(x, y, 255, 255, 255);
 };
 // Large sizes use the proportions of packaging/flatpak/io.github.tcottrill.Snip.svg;
 // small ones need longer, thicker marks to stay readable.
 const bool large = s >= 48;
 const int in = s * 22 / 100, len = std::max(2, s * (large ? 19 : 28) / 100), t = std::max(1, large ? s * 8 / 100 : s / 10);
 const int lo = in, hi = s - in;
 fill(lo, lo, len, t);           fill(lo, lo, t, len);
 fill(hi - len, lo, len, t);     fill(hi - t, lo, t, len);
 fill(lo, hi - t, len, t);       fill(lo, hi - len, t, len);
 fill(hi - len, hi - t, len, t); fill(hi - t, hi - len, t, len);
 return image;
}

wxIconBundle appIcons()
{
 wxIconBundle icons;
 for (int s : { 16, 20, 24, 32, 40, 48, 64, 128, 256 }) {
  wxIcon icon;
  icon.CopyFromBitmap(wxBitmap(appIconImage(s)));
  icons.AddIcon(icon);
 }
 return icons;
}

}
