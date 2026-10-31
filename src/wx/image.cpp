#include "snip.h"
#include "app_icon.h"
#include <wx/mstream.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

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

// The 256 px icon from packaging/icons (tools/make-icons.py), compiled in, scaled to s.
wxImage appIconImage(int s)
{
 wxMemoryInputStream in(appIconPng, sizeof appIconPng);
 wxImage image(in, wxBITMAP_TYPE_PNG);
 if (image.IsOk() && image.GetWidth() != s) image.Rescale(s, s, wxIMAGE_QUALITY_HIGH);
 return image;
}

wxIconBundle appIcons()
{
#ifdef __WXMSW__
 // Every size of the exe's "appicon" group (packaging/icons/freesnip.ico): wx gives the title
 // bar, taskbar and tray the size for the window's DPI.
 wxIconBundle resource(wxString("appicon"), static_cast<WXHINSTANCE>(::GetModuleHandleW(nullptr)));
 if (!resource.IsEmpty()) return resource;
#endif
 wxIconBundle icons;
 for (int s : { 16, 20, 24, 32, 40, 48, 64, 128, 256 }) {
  wxIcon icon;
  icon.CopyFromBitmap(wxBitmap(appIconImage(s)));
  icons.AddIcon(icon);
 }
 return icons;
}

}
