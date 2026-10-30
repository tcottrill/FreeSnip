// -----------------------------------------------------------------------------
// snip - a tiny screen snipping tool.
//
// Hotkey (PrtScn / Win+Shift+S / Ctrl+Alt+S, whichever are free) or a click on
// the tray icon freezes the screen; drag a rectangle and the snip goes onto the
// clipboard (PNG + DIB) and into a preview window with New / Copy / Save As.
//
// Running snip.exe opens the capture GUI (or restores the running instance).
// "/snip" starts a capture immediately. "/tray" starts it
// without snipping (used by "Start with Windows").
// -----------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

#include "monitor_select.h"   // windows.h with WIN32_LEAN_AND_MEAN + NOMINMAX

#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_APP_SNIP = WM_APP + 2;
constexpr UINT WM_APP_SHOW = WM_APP + 3;
constexpr UINT_PTR TIMER_SNIP = 1;

const wchar_t kHostClass[] = L"SnipToolHost";
const wchar_t kOverlayClass[] = L"SnipToolOverlay";
const wchar_t kPreviewClass[] = L"SnipToolPreview";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"Snip";

enum : UINT {
	ID_NEW = 101, ID_COPY, ID_SAVE, ID_CLOSE, ID_WINDOW,
	ID_TRAY_SNIP = 201, ID_TRAY_SHOW, ID_TRAY_STARTUP, ID_TRAY_EXIT,
};

struct Image {
	int w = 0, h = 0;
	std::vector<uint32_t> px;   // top-down BGRA
	std::vector<BYTE> png;
};

struct Overlay {
	int vx = 0, vy = 0, vw = 0, vh = 0;          // virtual screen rect
	HDC scrDC = nullptr, dimDC = nullptr, backDC = nullptr;
	HBITMAP scrBmp = nullptr, dimBmp = nullptr, backBmp = nullptr;
	HGDIOBJ scrOld = nullptr, dimOld = nullptr, backOld = nullptr;
	uint32_t* scrBits = nullptr;
	HFONT font = nullptr;
	HGDIOBJ fontOld = nullptr;
	UINT dpi = 96;
	bool dragging = false;
	bool windowPressed = false;
	std::vector<RECT> windows;                    // frozen top-to-bottom Z order, client coords
	RECT windowRect{};
	POINT a{}, b{};                               // drag anchor / current, client coords
};

struct Preview {
	HWND btnNew = nullptr, btnWindow = nullptr, btnCopy = nullptr, btnSave = nullptr, status = nullptr;
	HFONT font = nullptr;
	UINT dpi = 96;
	bool placing = false;                         // ignore WM_DPICHANGED's suggested rect
};

HINSTANCE g_inst;
HWND g_host, g_overlay, g_preview;
HICON g_iconBig, g_iconSmall;
HACCEL g_accel;
HHOOK g_keyboardHook = nullptr;
HANDLE g_keyboardThread = nullptr;
DWORD g_keyboardThreadId = 0;
bool g_shortcutHeld = false;
UINT g_taskbarCreated;
std::wstring g_hotkeys;
Image g_img;
bool g_previewWasVisible, g_snipPending;
bool g_windowMode = false;
std::wstring g_lastDir;
Overlay g_ov;
Preview g_pv;

int Sc(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }

HBITMAP CreateDib32(int w, int h, uint32_t** bits)
{
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* p = nullptr;
	HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
	*bits = static_cast<uint32_t*>(p);
	return b;
}

HFONT MakeUiFont(UINT dpi, int percent)
{
	NONCLIENTMETRICSW ncm{};
	ncm.cbSize = sizeof(ncm);
	SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
	ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * percent / 100;
	return CreateFontIndirectW(&ncm.lfMessageFont);
}

// Blue rounded square with white crop corners.
HICON MakeIcon(int s)
{
	uint32_t* px = nullptr;
	HBITMAP color = CreateDib32(s, s, &px);
	if (!color) return nullptr;
	const uint32_t blue = 0xFF2563EB, white = 0xFFFFFFFF;
	const float r = s / 5.0f;
	for (int y = 0; y < s; ++y) {
		for (int x = 0; x < s; ++x) {
			float fx = x + 0.5f, fy = y + 0.5f;
			float dx = std::max({ r - fx, fx - (s - r), 0.0f });
			float dy = std::max({ r - fy, fy - (s - r), 0.0f });
			px[y * s + x] = (dx * dx + dy * dy <= r * r) ? blue : 0;
		}
	}
	auto fill = [&](int x0, int y0, int w, int h) {
		for (int y = y0; y < y0 + h; ++y)
			for (int x = x0; x < x0 + w; ++x)
				if (x >= 0 && y >= 0 && x < s && y < s) px[y * s + x] = white;
	};
	const int in = s * 22 / 100, len = std::max(2, s * 28 / 100), t = std::max(1, s / 10);
	const int lo = in, hi = s - in;
	fill(lo, lo, len, t);           fill(lo, lo, t, len);
	fill(hi - len, lo, len, t);     fill(hi - t, lo, t, len);
	fill(lo, hi - t, len, t);       fill(lo, hi - len, t, len);
	fill(hi - len, hi - t, len, t); fill(hi - t, hi - len, t, len);

	std::vector<BYTE> zeros(static_cast<size_t>((s + 15) / 16) * 2 * s, 0);
	HBITMAP mask = CreateBitmap(s, s, 1, 1, zeros.data());
	ICONINFO ii{ TRUE, 0, 0, mask, color };
	HICON icon = CreateIconIndirect(&ii);
	DeleteObject(mask);
	DeleteObject(color);
	return icon;
}

// ---- PNG + clipboard --------------------------------------------------------

bool GetPngClsid(CLSID* out)
{
	UINT n = 0, size = 0;
	Gdiplus::GetImageEncodersSize(&n, &size);
	if (!size) return false;
	std::vector<BYTE> buf(size);
	auto* enc = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
	if (Gdiplus::GetImageEncoders(n, size, enc) != Gdiplus::Ok) return false;
	for (UINT i = 0; i < n; ++i) {
		if (wcscmp(enc[i].MimeType, L"image/png") == 0) { *out = enc[i].Clsid; return true; }
	}
	return false;
}

bool EncodePng(Image& img)
{
	img.png.clear();
	CLSID clsid;
	if (img.px.empty() || !GetPngClsid(&clsid)) return false;
	IStream* stream = nullptr;
	if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
	bool ok = false;
	{
		Gdiplus::Bitmap bmp(img.w, img.h, img.w * 4, PixelFormat32bppRGB,
		                    reinterpret_cast<BYTE*>(img.px.data()));
		if (bmp.Save(stream, &clsid, nullptr) == Gdiplus::Ok) {
			STATSTG st{};
			LARGE_INTEGER zero{};
			ULONG read = 0;
			if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME)) &&
			    SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) {
				img.png.resize(static_cast<size_t>(st.cbSize.QuadPart));
				ok = SUCCEEDED(stream->Read(img.png.data(), static_cast<ULONG>(img.png.size()), &read)) &&
				     read == img.png.size();
			}
		}
	}
	stream->Release();
	if (!ok) img.png.clear();
	return ok;
}

// Puts both a "PNG" clipboard format (browsers / Electron apps prefer it) and a
// 24-bit CF_DIB (everything else) on the clipboard.
bool CopyImageToClipboard(HWND owner, const Image& img)
{
	if (img.px.empty()) return false;
	const int stride = (img.w * 3 + 3) & ~3;
	const SIZE_T dibSize = sizeof(BITMAPINFOHEADER) + static_cast<SIZE_T>(stride) * img.h;
	HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dibSize);
	if (!hDib) return false;
	{
		BYTE* p = static_cast<BYTE*>(GlobalLock(hDib));
		auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(p);
		bih->biSize = sizeof(*bih);
		bih->biWidth = img.w;
		bih->biHeight = img.h;   // bottom-up
		bih->biPlanes = 1;
		bih->biBitCount = 24;
		bih->biCompression = BI_RGB;
		bih->biSizeImage = static_cast<DWORD>(stride) * img.h;
		BYTE* bits = p + sizeof(*bih);
		for (int y = 0; y < img.h; ++y) {
			BYTE* row = bits + static_cast<SIZE_T>(img.h - 1 - y) * stride;
			const uint32_t* src = &img.px[static_cast<size_t>(y) * img.w];
			for (int x = 0; x < img.w; ++x) {
				row[x * 3 + 0] = static_cast<BYTE>(src[x]);
				row[x * 3 + 1] = static_cast<BYTE>(src[x] >> 8);
				row[x * 3 + 2] = static_cast<BYTE>(src[x] >> 16);
			}
		}
		GlobalUnlock(hDib);
	}
	HGLOBAL hPng = nullptr;
	if (!img.png.empty() && (hPng = GlobalAlloc(GMEM_MOVEABLE, img.png.size())) != nullptr) {
		memcpy(GlobalLock(hPng), img.png.data(), img.png.size());
		GlobalUnlock(hPng);
	}

	bool open = false;
	for (int i = 0; i < 10 && !(open = OpenClipboard(owner) != FALSE); ++i) Sleep(25);
	if (!open) {
		GlobalFree(hDib);
		if (hPng) GlobalFree(hPng);
		return false;
	}
	EmptyClipboard();
	static const UINT cfPng = RegisterClipboardFormatW(L"PNG");
	if (hPng && !SetClipboardData(cfPng, hPng)) GlobalFree(hPng);
	const bool ok = SetClipboardData(CF_DIB, hDib) != nullptr;
	if (!ok) GlobalFree(hDib);
	CloseClipboard();
	return ok;
}

// ---- Preview window ---------------------------------------------------------

int ToolbarHeight(UINT dpi) { return Sc(44, dpi); }

void SetStatus(const std::wstring& s) { if (g_pv.status) SetWindowTextW(g_pv.status, s.c_str()); }

void ApplyPreviewFont()
{
	HFONT nf = MakeUiFont(g_pv.dpi, 100);
	for (HWND c : { g_pv.btnNew, g_pv.btnWindow, g_pv.btnCopy, g_pv.btnSave, g_pv.status })
		SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(nf), TRUE);
	if (g_pv.font) DeleteObject(g_pv.font);
	g_pv.font = nf;
}

void LayoutPreview(HWND hwnd)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const UINT d = g_pv.dpi;
	const int pad = Sc(8, d), bh = Sc(28, d), bw = Sc(88, d), gap = Sc(6, d);
	int x = pad;
	MoveWindow(g_pv.btnNew, x, pad, bw, bh, TRUE);  x += bw + gap;
	MoveWindow(g_pv.btnWindow, x, pad, bw, bh, TRUE); x += bw + gap;
	MoveWindow(g_pv.btnCopy, x, pad, bw, bh, TRUE); x += bw + gap;
	MoveWindow(g_pv.btnSave, x, pad, bw, bh, TRUE); x += bw + Sc(12, d);
	MoveWindow(g_pv.status, x, pad, std::max(0, static_cast<int>(rc.right) - pad - x), bh, TRUE);
}

void SaveAs()
{
	if (g_img.png.empty()) { SetStatus(L"Nothing to save"); return; }
	SYSTEMTIME t;
	GetLocalTime(&t);
	wchar_t file[MAX_PATH];
	swprintf_s(file, L"Snip %04u-%02u-%02u %02u%02u%02u.png",
	           t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	if (g_lastDir.empty()) {
		PWSTR p = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &p))) g_lastDir = p;
		CoTaskMemFree(p);
	}
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_preview;
	ofn.lpstrFilter = L"PNG image (*.png)\0*.png\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrInitialDir = g_lastDir.empty() ? nullptr : g_lastDir.c_str();
	ofn.lpstrDefExt = L"png";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&ofn)) return;

	HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	const bool ok = f != INVALID_HANDLE_VALUE &&
	                WriteFile(f, g_img.png.data(), static_cast<DWORD>(g_img.png.size()), &written, nullptr) &&
	                written == g_img.png.size();
	if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
	if (ok) {
		g_lastDir.assign(file, ofn.nFileOffset);
		SetStatus(std::wstring(L"Saved ") + file);
	} else {
		SetStatus(L"Save failed");
	}
}

void StartSnip(UINT delayMs, bool windowMode = false);

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE: {
		auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, UINT id) {
			return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
			                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), g_inst, nullptr);
		};
		g_pv.btnNew = mk(L"BUTTON", L"New", BS_PUSHBUTTON | WS_TABSTOP, ID_NEW);
		g_pv.btnWindow = mk(L"BUTTON", L"Window", BS_PUSHBUTTON | WS_TABSTOP, ID_WINDOW);
		g_pv.btnCopy = mk(L"BUTTON", L"Copy", BS_PUSHBUTTON | WS_TABSTOP, ID_COPY);
		g_pv.btnSave = mk(L"BUTTON", L"Save As...", BS_PUSHBUTTON | WS_TABSTOP, ID_SAVE);
		g_pv.status = mk(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_PATHELLIPSIS, 0);
		g_pv.dpi = GetDpiForWindow(hwnd);
		ApplyPreviewFont();
		BOOL off = TRUE;
		DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof(off));
		return 0;
	}
	case WM_SIZE:
		LayoutPreview(hwnd);
		return 0;
	case WM_GETMINMAXINFO: {
		auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
		mmi->ptMinTrackSize = { Sc(500, g_pv.dpi), Sc(160, g_pv.dpi) };
		return 0;
	}
	case WM_DPICHANGED: {
		g_pv.dpi = HIWORD(wp);
		ApplyPreviewFont();
		if (!g_pv.placing) {
			const RECT* r = reinterpret_cast<const RECT*>(lp);
			SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
			             SWP_NOZORDER | SWP_NOACTIVATE);
		}
		LayoutPreview(hwnd);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		const int cw = rc.right, ch = rc.bottom;
		HDC mem = CreateCompatibleDC(dc);
		HBITMAP mb = CreateCompatibleBitmap(dc, std::max(cw, 1), std::max(ch, 1));
		HGDIOBJ old = SelectObject(mem, mb);

		const UINT d = g_pv.dpi;
		const int tb = ToolbarHeight(d), pad = Sc(8, d);
		RECT top{ 0, 0, cw, tb };
		FillRect(mem, &top, GetSysColorBrush(COLOR_BTNFACE));
		RECT area{ 0, tb, cw, ch };
		HBRUSH bg = CreateSolidBrush(g_img.px.empty() ? RGB(248, 248, 248) : RGB(204, 204, 204));
		FillRect(mem, &area, bg);
		DeleteObject(bg);

		const int aw = cw - 2 * pad, ah = ch - tb - 2 * pad;
		if (g_img.px.empty()) {
			HGDIOBJ oldFont = SelectObject(mem, g_pv.font);
			SetBkMode(mem, TRANSPARENT);
			SetTextColor(mem, RGB(65, 65, 65));
			RECT textArea{ pad, tb + (ch - tb) / 2 - Sc(30, d), cw - pad, ch - pad };
			const std::wstring hint = std::wstring(L"Choose New to snip a rectangle, or Window to capture an application.\n\n") +
				(g_hotkeys.find(L"Win+Shift+S") != std::wstring::npos
				 ? L"Win+Shift+S starts a snip from any app."
				 : L"Your snip will be copied automatically and appear here.");
			DrawTextW(mem, hint.c_str(),
			          -1, &textArea, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
			SelectObject(mem, oldFont);
		}
		if (!g_img.px.empty() && aw > 0 && ah > 0) {
			const double sc = std::min({ 1.0, static_cast<double>(aw) / g_img.w, static_cast<double>(ah) / g_img.h });
			const int dw = std::max(1, static_cast<int>(g_img.w * sc));
			const int dh = std::max(1, static_cast<int>(g_img.h * sc));
			const int x = (cw - dw) / 2, y = tb + pad + (ah - dh) / 2;
			BITMAPINFO bi{};
			bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
			bi.bmiHeader.biWidth = g_img.w;
			bi.bmiHeader.biHeight = -g_img.h;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bi.bmiHeader.biCompression = BI_RGB;
			SetStretchBltMode(mem, sc < 1.0 ? HALFTONE : COLORONCOLOR);
			SetBrushOrgEx(mem, 0, 0, nullptr);
			StretchDIBits(mem, x, y, dw, dh, 0, 0, g_img.w, g_img.h, g_img.px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
			RECT frame{ x - 1, y - 1, x + dw + 1, y + dh + 1 };
			HBRUSH fb = CreateSolidBrush(RGB(128, 128, 128));
			FrameRect(mem, &frame, fb);
			DeleteObject(fb);
		}
		BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
		SelectObject(mem, old);
		DeleteObject(mb);
		DeleteDC(mem);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_CTLCOLORSTATIC:
		SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_BTNFACE));
		return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case ID_NEW:   StartSnip(0); break;
		case ID_WINDOW: StartSnip(0, true); break;
		case ID_COPY:
			if (!g_img.px.empty()) SetStatus(CopyImageToClipboard(hwnd, g_img) ? L"Copied to clipboard" : L"Clipboard busy, try again");
			break;
		case ID_SAVE: if (!g_img.png.empty()) SaveAs(); break;
		case ID_CLOSE: ShowWindow(hwnd, SW_HIDE); break;
		}
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);   // keep running in the tray
		return 0;
	case WM_DESTROY:
		if (g_pv.font) DeleteObject(g_pv.font);
		g_pv = Preview{};
		g_preview = nullptr;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

// Centers the preview on the monitor the snip came from.
void ShowPreview(const RECT& snipVirtual, bool copied)
{
	if (!g_preview) {
		g_preview = CreateWindowExW(0, kPreviewClass, L"Snip", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		                            CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, nullptr, nullptr, g_inst, nullptr);
		if (!g_preview) return;
	}
	HMONITOR hm = MonitorFromRect(&snipVirtual, MONITOR_DEFAULTTONEAREST);
	std::vector<monsel::MonitorDesc> mons = monsel::Enumerate();
	monsel::MonitorDesc m = mons.front();
	for (const auto& d : mons) if (d.handle == hm) m = d;
	UINT dpi = 96, dpiY = 96;
	GetDpiForMonitor(hm, MDT_EFFECTIVE_DPI, &dpi, &dpiY);

	const int pad = Sc(8, dpi);
	const int cw = std::max(g_img.w + 2 * pad, Sc(580, dpi));
	const int ch = ToolbarHeight(dpi) + std::max(g_img.h, Sc(g_img.px.empty() ? 240 : 60, dpi)) + 2 * pad;
	RECT wr{ 0, 0, cw, ch };
	AdjustWindowRectExForDpi(&wr, static_cast<DWORD>(GetWindowLongPtrW(g_preview, GWL_STYLE)), FALSE,
	                         static_cast<DWORD>(GetWindowLongPtrW(g_preview, GWL_EXSTYLE)), dpi);
	const int ow = std::min<int>(wr.right - wr.left, (m.workRect.right - m.workRect.left) * 9 / 10);
	const int oh = std::min<int>(wr.bottom - wr.top, (m.workRect.bottom - m.workRect.top) * 9 / 10);
	const RECT r = monsel::CenterInWorkArea(ow, oh, m);

	if (IsZoomed(g_preview) || IsIconic(g_preview)) ShowWindow(g_preview, SW_RESTORE);
	g_pv.placing = true;
	SetWindowPos(g_preview, HWND_TOP, r.left, r.top, ow, oh, SWP_SHOWWINDOW);
	g_pv.placing = false;
	if (GetDpiForWindow(g_preview) != g_pv.dpi) {
		g_pv.dpi = GetDpiForWindow(g_preview);
		ApplyPreviewFont();
		LayoutPreview(g_preview);
	}

	wchar_t title[64];
	if (g_img.px.empty()) wcscpy_s(title, L"Snip");
	else swprintf_s(title, L"Snip - %d x %d", g_img.w, g_img.h);
	SetWindowTextW(g_preview, title);
	EnableWindow(g_pv.btnCopy, !g_img.px.empty());
	EnableWindow(g_pv.btnSave, !g_img.png.empty());
	SetStatus(g_img.px.empty() ? L"Ready" : (copied ? L"Copied to clipboard" : L"Clipboard busy, press Copy"));
	InvalidateRect(g_preview, nullptr, FALSE);
	SetForegroundWindow(g_preview);
}

void ShowMainWindow()
{
	if (g_overlay || g_snipPending) return;
	if (!g_preview) {
		ShowPreview(monsel::Primary().monitorRect, false);
	} else {
		ShowWindow(g_preview, IsIconic(g_preview) ? SW_RESTORE : SW_SHOW);
		SetForegroundWindow(g_preview);
	}
}

// ---- Overlay (the frozen, dimmed screen you drag on) ------------------------

void FreeOverlay()
{
	Overlay& o = g_ov;
	if (o.backDC && o.fontOld) SelectObject(o.backDC, o.fontOld);
	if (o.scrDC) { SelectObject(o.scrDC, o.scrOld); DeleteDC(o.scrDC); }
	if (o.dimDC) { SelectObject(o.dimDC, o.dimOld); DeleteDC(o.dimDC); }
	if (o.backDC) { SelectObject(o.backDC, o.backOld); DeleteDC(o.backDC); }
	if (o.scrBmp) DeleteObject(o.scrBmp);
	if (o.dimBmp) DeleteObject(o.dimBmp);
	if (o.backBmp) DeleteObject(o.backBmp);
	if (o.font) DeleteObject(o.font);
	o = Overlay{};
}

RECT SelRect()
{
	const Overlay& o = g_ov;
	if (g_windowMode) return o.windowRect;
	RECT r{ std::min(o.a.x, o.b.x), std::min(o.a.y, o.b.y),
	        std::max(o.a.x, o.b.x) + 1, std::max(o.a.y, o.b.y) + 1 };
	r.left = std::clamp<LONG>(r.left, 0, o.vw);
	r.right = std::clamp<LONG>(r.right, 0, o.vw);
	r.top = std::clamp<LONG>(r.top, 0, o.vh);
	r.bottom = std::clamp<LONG>(r.bottom, 0, o.vh);
	return r;
}

std::wstring SizeText(const RECT& s)
{
	wchar_t buf[64];
	swprintf_s(buf, L"%ld x %ld", s.right - s.left, s.bottom - s.top);
	return buf;
}

RECT TextBox(const std::wstring& text, int x, int y)
{
	SIZE sz{};
	GetTextExtentPoint32W(g_ov.backDC, text.c_str(), static_cast<int>(text.size()), &sz);
	return RECT{ x, y, x + sz.cx + 2 * Sc(8, g_ov.dpi), y + sz.cy + 2 * Sc(4, g_ov.dpi) };
}

RECT LabelRect(const RECT& s)
{
	const std::wstring t = SizeText(s);
	RECT r = TextBox(t, s.left, 0);
	const int h = r.bottom - r.top, gap = Sc(6, g_ov.dpi);
	if (s.top - h - gap >= 0) OffsetRect(&r, 0, s.top - h - gap);
	else OffsetRect(&r, gap, s.top + gap);
	return r;
}

int FrameWidth() { return std::max(1, Sc(2, g_ov.dpi)); }

RECT DecorRect(const RECT& s)
{
	RECT f = s;
	InflateRect(&f, FrameWidth() + 1, FrameWidth() + 1);
	RECT l = LabelRect(s), u;
	UnionRect(&u, &f, &l);
	return u;
}

const wchar_t* CaptureHint()
{
	return g_windowMode
		? L"Click a window     R: rectangle     Enter: whole monitor     Esc: cancel"
		: L"Drag to snip     W: window     Enter: whole monitor     Esc: cancel";
}

// EnumWindows gives top-level windows in Z order. Freeze their bounds with the
// desktop so hover selection cannot drift if an application moves afterward.
BOOL CALLBACK CollectCaptureWindow(HWND hwnd, LPARAM)
{
	if (hwnd == g_host || hwnd == g_preview || hwnd == g_overlay ||
	    hwnd == GetShellWindow() || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
	DWORD cloaked = 0;
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
		return TRUE;
	wchar_t cls[128]{};
	GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
	if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
	    !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) return TRUE;
	RECT r{};
	if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) || IsRectEmpty(&r)) {
		if (!GetWindowRect(hwnd, &r)) return TRUE;
	}
	OffsetRect(&r, -g_ov.vx, -g_ov.vy);
	const RECT desktop{ 0, 0, g_ov.vw, g_ov.vh };
	RECT clipped{};
	if (IntersectRect(&clipped, &r, &desktop)) g_ov.windows.push_back(clipped);
	return TRUE;
}

void SelectWindowAt(POINT pt)
{
	g_ov.windowRect = RECT{};
	for (const RECT& r : g_ov.windows) {
		if (PtInRect(&r, pt)) { g_ov.windowRect = r; break; }
	}
}

RECT HintRect()
{
	const RECT m = monsel::Primary().monitorRect;
	RECT r = TextBox(CaptureHint(), 0, 0);
	const int w = r.right - r.left;
	const int x = (m.left + m.right) / 2 - w / 2 - g_ov.vx;
	const int y = m.top + Sc(24, g_ov.dpi) - g_ov.vy;
	OffsetRect(&r, x, y);
	return r;
}

void DrawTextBox(HDC dc, const RECT& r, const std::wstring& text)
{
	HBRUSH b = CreateSolidBrush(RGB(32, 32, 32));
	FillRect(dc, &r, b);
	DeleteObject(b);
	RECT t = r;
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void InvalidateOverlay(const RECT& r) { InvalidateRect(g_overlay, &r, FALSE); }

void CancelSnip()
{
	if (g_overlay) DestroyWindow(g_overlay);
	if (g_previewWasVisible && g_preview) {
		ShowWindow(g_preview, SW_SHOW);
		SetForegroundWindow(g_preview);
	}
}

void FinishSnip(const RECT& s)
{
	const Overlay& o = g_ov;
	Image img;
	img.w = s.right - s.left;
	img.h = s.bottom - s.top;
	if (img.w <= 0 || img.h <= 0) { CancelSnip(); return; }
	img.px.resize(static_cast<size_t>(img.w) * img.h);
	for (int y = 0; y < img.h; ++y) {
		const uint32_t* src = o.scrBits + static_cast<size_t>(s.top + y) * o.vw + s.left;
		uint32_t* dst = &img.px[static_cast<size_t>(y) * img.w];
		for (int x = 0; x < img.w; ++x) dst[x] = src[x] | 0xFF000000u;
	}
	RECT vs = s;
	OffsetRect(&vs, o.vx, o.vy);
	DestroyWindow(g_overlay);

	g_img = std::move(img);
	EncodePng(g_img);
	const bool copied = CopyImageToClipboard(g_host, g_img);
	ShowPreview(vs, copied);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	Overlay& o = g_ov;
	switch (msg) {
	case WM_CREATE:
		o.dpi = GetDpiForWindow(hwnd);
		o.font = MakeUiFont(o.dpi, 115);
		o.fontOld = SelectObject(o.backDC, o.font);
		return 0;
	case WM_DPICHANGED:
		return 0;   // the overlay always spans the whole virtual screen; never resize
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		const RECT pr = ps.rcPaint;
		const int pw = pr.right - pr.left, ph = pr.bottom - pr.top;
		HDC b = o.backDC;
		BitBlt(b, pr.left, pr.top, pw, ph, o.dimDC, pr.left, pr.top, SRCCOPY);
		if (o.dragging || (g_windowMode && !IsRectEmpty(&o.windowRect))) {
			const RECT s = SelRect();
			RECT i;
			if (IntersectRect(&i, &s, &pr))
				BitBlt(b, i.left, i.top, i.right - i.left, i.bottom - i.top, o.scrDC, i.left, i.top, SRCCOPY);
			HBRUSH blue = CreateSolidBrush(RGB(0x25, 0x63, 0xEB));
			for (int k = 1; k <= FrameWidth(); ++k) {
				RECT f = s;
				InflateRect(&f, k, k);
				FrameRect(b, &f, blue);
			}
			DeleteObject(blue);
			DrawTextBox(b, LabelRect(s), SizeText(s));
		}
		if (!o.dragging) DrawTextBox(b, HintRect(), CaptureHint());
		BitBlt(dc, pr.left, pr.top, pw, ph, b, pr.left, pr.top, SRCCOPY);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_LBUTTONDOWN:
		if (g_windowMode) {
			SelectWindowAt({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
			o.windowPressed = !IsRectEmpty(&o.windowRect);
			if (o.windowPressed) SetCapture(hwnd);
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}
		InvalidateOverlay(HintRect());
		o.dragging = true;
		o.a = o.b = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		SetCapture(hwnd);
		InvalidateOverlay(DecorRect(SelRect()));
		return 0;
	case WM_MOUSEMOVE:
		if (g_windowMode) {
			const RECT before = o.windowRect;
			SelectWindowAt({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
			if (!EqualRect(&before, &o.windowRect)) {
				InvalidateOverlay(DecorRect(before));
				InvalidateOverlay(DecorRect(o.windowRect));
				InvalidateOverlay(HintRect());
			}
			return 0;
		}
		if (o.dragging) {
			const RECT before = DecorRect(SelRect());
			o.b = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
			InvalidateOverlay(before);
			InvalidateOverlay(DecorRect(SelRect()));
		}
		return 0;
	case WM_LBUTTONUP: {
		if (g_windowMode) {
			const bool pressed = o.windowPressed;
			o.windowPressed = false;
			ReleaseCapture();
			if (!pressed) return 0;
			SelectWindowAt({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
			if (!IsRectEmpty(&o.windowRect)) FinishSnip(o.windowRect);
			return 0;
		}
		if (!o.dragging) return 0;
		ReleaseCapture();
		const RECT s = SelRect();
		if (s.right - s.left < 3 && s.bottom - s.top < 3) {   // a click, not a drag
			InvalidateOverlay(DecorRect(s));
			o.dragging = false;
			InvalidateOverlay(HintRect());
			return 0;
		}
		FinishSnip(s);
		return 0;
	}
	case WM_RBUTTONDOWN:
		if (o.dragging) {
			ReleaseCapture();
			InvalidateOverlay(DecorRect(SelRect()));
			o.dragging = false;
			InvalidateOverlay(HintRect());
		} else {
			CancelSnip();
		}
		return 0;
	case WM_KEYDOWN:
		if (wp == 'W' || wp == 'R') {
			o.windowPressed = false;
			ReleaseCapture();
			o.dragging = false;
			g_windowMode = wp == 'W';
			POINT pt{};
			GetCursorPos(&pt);
			ScreenToClient(hwnd, &pt);
			SelectWindowAt(pt);
			InvalidateRect(hwnd, nullptr, FALSE);
		} else if (wp == VK_ESCAPE) {
			CancelSnip();
		} else if (wp == VK_RETURN) {
			POINT pt;
			GetCursorPos(&pt);
			MONITORINFO mi{ sizeof(mi) };
			GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
			RECT s = mi.rcMonitor;
			OffsetRect(&s, -o.vx, -o.vy);
			FinishSnip(s);
		}
		return 0;
	case WM_CAPTURECHANGED:
		o.windowPressed = false;
		return 0;
	case WM_DESTROY:
		FreeOverlay();
		g_overlay = nullptr;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

bool BeginOverlay()
{
	Overlay& o = g_ov;
	FreeOverlay();
	o.vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
	o.vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
	o.vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	o.vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	EnumWindows(CollectCaptureWindow, 0);
	POINT cursor{};
	GetCursorPos(&cursor);
	cursor.x -= o.vx;
	cursor.y -= o.vy;
	SelectWindowAt(cursor);

	HDC screen = GetDC(nullptr);
	uint32_t* dimBits = nullptr;
	o.scrBmp = CreateDib32(o.vw, o.vh, &o.scrBits);
	o.dimBmp = CreateDib32(o.vw, o.vh, &dimBits);
	o.backBmp = CreateCompatibleBitmap(screen, o.vw, o.vh);
	if (!o.scrBmp || !o.dimBmp || !o.backBmp) {
		ReleaseDC(nullptr, screen);
		FreeOverlay();
		return false;
	}
	o.scrDC = CreateCompatibleDC(screen);
	o.dimDC = CreateCompatibleDC(screen);
	o.backDC = CreateCompatibleDC(screen);
	o.scrOld = SelectObject(o.scrDC, o.scrBmp);
	o.dimOld = SelectObject(o.dimDC, o.dimBmp);
	o.backOld = SelectObject(o.backDC, o.backBmp);
	BitBlt(o.scrDC, 0, 0, o.vw, o.vh, screen, o.vx, o.vy, SRCCOPY | CAPTUREBLT);
	ReleaseDC(nullptr, screen);
	GdiFlush();

	const size_t n = static_cast<size_t>(o.vw) * o.vh;
	for (size_t i = 0; i < n; ++i) dimBits[i] = (o.scrBits[i] >> 1) & 0x7F7F7Fu;
	BitBlt(o.backDC, 0, 0, o.vw, o.vh, o.dimDC, 0, 0, SRCCOPY);

	g_overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClass, L"Snip", WS_POPUP,
	                            o.vx, o.vy, o.vw, o.vh, nullptr, nullptr, g_inst, nullptr);
	if (!g_overlay) { FreeOverlay(); return false; }
	BOOL off = TRUE;
	DwmSetWindowAttribute(g_overlay, DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof(off));
	ShowWindow(g_overlay, SW_SHOW);
	UpdateWindow(g_overlay);
	SetForegroundWindow(g_overlay);
	SetFocus(g_overlay);
	return true;
}

void StartSnip(UINT delayMs, bool windowMode)
{
	if (g_overlay || g_snipPending) return;
	g_windowMode = windowMode;
	g_previewWasVisible = g_preview && IsWindowVisible(g_preview);
	if (g_previewWasVisible) {
		ShowWindow(g_preview, SW_HIDE);
		delayMs = std::max<UINT>(delayMs, 150);
	}
	if (delayMs) {
		g_snipPending = true;
		SetTimer(g_host, TIMER_SNIP, delayMs, nullptr);
	} else if (!BeginOverlay()) {
		CancelSnip();
	}
}

// ---- Tray host --------------------------------------------------------------

bool IsStartupEnabled()
{
	HKEY k;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
	const bool on = RegQueryValueExW(k, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
	RegCloseKey(k);
	return on;
}

void SetStartup(bool on)
{
	HKEY k;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
		return;
	if (on) {
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		const std::wstring cmd = L"\"" + std::wstring(exe) + L"\" /tray";
		RegSetValueExW(k, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
		               static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
	} else {
		RegDeleteValueW(k, kRunValue);
	}
	RegCloseKey(k);
}

void AddTrayIcon()
{
	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_host;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAY;
	nid.hIcon = g_iconSmall;
	const std::wstring tip = g_hotkeys.empty() ? L"Snip - click to snip" : L"Snip - click or " + g_hotkeys;
	wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
	Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon()
{
	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_host;
	nid.uID = 1;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

// Only intercept the requested chord when RegisterHotKey cannot claim it.
// Keep this callback quick: capture work runs later on the main message loop.
LRESULT CALLBACK CaptureKeyboardProc(int code, WPARAM wp, LPARAM lp)
{
	if (code == HC_ACTION) {
		const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
		if (key->vkCode == 'S') {
			const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
			const bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
			if (up && g_shortcutHeld) { g_shortcutHeld = false; return 1; }
			const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
			const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			const bool extra = (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000);
			if (down && (g_shortcutHeld || (win && shift && !extra))) {
				if (!g_shortcutHeld) PostMessageW(g_host, WM_APP_SNIP, 0, 0);
				g_shortcutHeld = true;
				return 1;
			}
		}
	}
	return CallNextHookEx(g_keyboardHook, code, wp, lp);
}

DWORD WINAPI KeyboardThreadProc(void* readyEvent)
{
	MSG msg{};
	PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE); // create thread message queue
	g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureKeyboardProc, g_inst, 0);
	SetEvent(static_cast<HANDLE>(readyEvent));
	if (!g_keyboardHook) return 0;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	UnhookWindowsHookEx(g_keyboardHook);
	return 0;
}

bool InstallCaptureKeyboardHook()
{
	HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!ready) return false;
	g_keyboardThread = CreateThread(nullptr, 0, KeyboardThreadProc, ready, 0, &g_keyboardThreadId);
	if (g_keyboardThread) WaitForSingleObject(ready, INFINITE);
	CloseHandle(ready);
	return g_keyboardHook != nullptr;
}

void UnregisterCaptureHotkeys()
{
	for (int id = 1; id <= 3; ++id) UnregisterHotKey(g_host, id);
	if (g_keyboardThread) {
		PostThreadMessageW(g_keyboardThreadId, WM_QUIT, 0, 0);
		WaitForSingleObject(g_keyboardThread, INFINITE);
		CloseHandle(g_keyboardThread);
	}
	g_keyboardThread = nullptr;
	g_keyboardThreadId = 0;
	g_keyboardHook = nullptr;
	g_shortcutHeld = false;
}

void RegisterHotkeys()
{
	struct Hk { UINT mod, vk; const wchar_t* name; };
	const Hk hks[] = {
		{ 0, VK_SNAPSHOT, L"PrtScn" },
		{ MOD_WIN | MOD_SHIFT, 'S', L"Win+Shift+S" },
		{ MOD_CONTROL | MOD_ALT, 'S', L"Ctrl+Alt+S" },
	};
	for (int i = 0; i < static_cast<int>(std::size(hks)); ++i) {
		bool available = RegisterHotKey(g_host, i + 1, hks[i].mod | MOD_NOREPEAT, hks[i].vk) != FALSE;
		if (!available && hks[i].mod == (MOD_WIN | MOD_SHIFT)) {
			available = InstallCaptureKeyboardHook();
		}
		if (available) {
			if (!g_hotkeys.empty()) g_hotkeys += L" / ";
			g_hotkeys += hks[i].name;
		}
	}
}

void ShowTrayMenu()
{
	HMENU m = CreatePopupMenu();
	const std::wstring snipItem = g_hotkeys.empty() ? L"New snip" : L"New snip\t" + g_hotkeys;
	AppendMenuW(m, MF_STRING, ID_TRAY_SNIP, snipItem.c_str());
	AppendMenuW(m, MF_STRING, ID_WINDOW, L"Capture window");
	AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"Open Snip");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0), ID_TRAY_STARTUP, L"Start with Windows");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Exit");
	SetMenuDefaultItem(m, ID_TRAY_SNIP, FALSE);

	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(g_host);
	const UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_host, nullptr);
	PostMessageW(g_host, WM_NULL, 0, 0);
	DestroyMenu(m);

	switch (cmd) {
	case ID_TRAY_SNIP: StartSnip(200); break;
	case ID_WINDOW: StartSnip(200, true); break;
	case ID_TRAY_SHOW:
		ShowMainWindow();
		break;
	case ID_TRAY_STARTUP: SetStartup(!IsStartupEnabled()); break;
	case ID_TRAY_EXIT: DestroyWindow(g_host); break;
	}
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == g_taskbarCreated && g_taskbarCreated) { AddTrayIcon(); return 0; }
	switch (msg) {
	case WM_CREATE:
		g_host = hwnd;
		RegisterHotkeys();
		AddTrayIcon();
		return 0;
	case WM_HOTKEY:
	case WM_APP_SNIP:
		StartSnip(0);
		return 0;
	case WM_APP_SHOW:
		ShowMainWindow();
		return 0;
	case WM_TIMER:
		if (wp == TIMER_SNIP) {
			KillTimer(hwnd, TIMER_SNIP);
			g_snipPending = false;
			if (!BeginOverlay()) CancelSnip();
		}
		return 0;
	case WM_TRAY:
		if (lp == WM_LBUTTONUP) StartSnip(150);
		else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowTrayMenu();
		return 0;
	case WM_DESTROY:
		UnregisterCaptureHotkeys();
		RemoveTrayIcon();
		if (g_overlay) DestroyWindow(g_overlay);
		if (g_preview) DestroyWindow(g_preview);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
	g_inst = inst;
	bool trayOnly = false, snipNow = false;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; argv && i < argc; ++i) {
		if (!_wcsicmp(argv[i], L"/tray") || !_wcsicmp(argv[i], L"-tray") || !_wcsicmp(argv[i], L"--tray")) trayOnly = true;
		if (!_wcsicmp(argv[i], L"/snip") || !_wcsicmp(argv[i], L"-snip") || !_wcsicmp(argv[i], L"--snip")) snipNow = true;
	}
	LocalFree(argv);

	HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\SnipTool.SingleInstance");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		HWND other = FindWindowW(kHostClass, nullptr);
		if (other && !trayOnly) {
			DWORD pid = 0;
			GetWindowThreadProcessId(other, &pid);
			AllowSetForegroundWindow(pid);
			PostMessageW(other, snipNow ? WM_APP_SNIP : WM_APP_SHOW, 0, 0);
		}
		return 0;
	}

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	ULONG_PTR gdiplusToken = 0;
	Gdiplus::GdiplusStartupInput gsi;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gsi, nullptr);

	g_iconBig = MakeIcon(GetSystemMetrics(SM_CXICON));
	g_iconSmall = MakeIcon(GetSystemMetrics(SM_CXSMICON));
	g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

	WNDCLASSEXW wc{ sizeof(wc) };
	wc.hInstance = inst;
	wc.hIcon = g_iconBig;
	wc.hIconSm = g_iconSmall;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

	wc.lpfnWndProc = HostProc;
	wc.lpszClassName = kHostClass;
	RegisterClassExW(&wc);

	wc.lpfnWndProc = OverlayProc;
	wc.lpszClassName = kOverlayClass;
	wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
	RegisterClassExW(&wc);

	wc.lpfnWndProc = PreviewProc;
	wc.lpszClassName = kPreviewClass;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClassExW(&wc);

	ACCEL acc[] = {
		{ FCONTROL | FVIRTKEY, 'N', ID_NEW },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'W', ID_WINDOW },
		{ FCONTROL | FVIRTKEY, 'C', ID_COPY },
		{ FCONTROL | FVIRTKEY, 'S', ID_SAVE },
		{ FVIRTKEY, VK_ESCAPE, ID_CLOSE },
	};
	g_accel = CreateAcceleratorTableW(acc, static_cast<int>(std::size(acc)));

	CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"Snip", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
	if (!g_host) return 1;
	if (!trayOnly) PostMessageW(g_host, snipNow ? WM_APP_SNIP : WM_APP_SHOW, 0, 0);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		if (g_preview && (msg.hwnd == g_preview || IsChild(g_preview, msg.hwnd)) &&
		    TranslateAcceleratorW(g_preview, g_accel, &msg))
			continue;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	DestroyAcceleratorTable(g_accel);
	DestroyIcon(g_iconBig);
	DestroyIcon(g_iconSmall);
	Gdiplus::GdiplusShutdown(gdiplusToken);
	CoUninitialize();
	CloseHandle(mutex);
	return 0;
}
