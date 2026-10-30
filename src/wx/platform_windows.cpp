// Windows: GDI desktop capture, PNG + DIB clipboard, global shortcuts on a hidden
// message window (also the single-instance target), the tray icon and the HKCU
// Run entry.
#include "snip.h"
#include <wx/taskbar.h>
#include <wx/msw/wrapwin.h>
#include "../monitor_select.h"
#include <dwmapi.h>
#include <cstring>

#pragma comment(lib, "dwmapi.lib")

namespace snip::platform {
namespace {

constexpr UINT WM_APP_SNIP = WM_APP + 2;
constexpr UINT WM_APP_SHOW = WM_APP + 3;
const wchar_t kHostClass[] = L"SnipToolHost";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"Snip";

HANDLE g_mutex = nullptr;
HWND g_host = nullptr;
std::function<void()> g_snip, g_show;
wxString g_names;
HHOOK g_keyboardHook = nullptr;
HANDLE g_keyboardThread = nullptr;
DWORD g_keyboardThreadId = 0;
bool g_shortcutHeld = false;

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
 switch (msg) {
 case WM_HOTKEY:
 case WM_APP_SNIP:
  if (g_snip) g_snip();
  return 0;
 case WM_APP_SHOW:
  if (g_show) g_show();
  return 0;
 }
 return DefWindowProcW(hwnd, msg, wp, lp);
}

// Only intercept Win+Shift+S when RegisterHotKey cannot claim it.
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
 g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureKeyboardProc, GetModuleHandleW(nullptr), 0);
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

struct WindowScan {
 std::vector<wxRect>* windows;
 RECT desktop;      // virtual screen
 DWORD self;
};

// EnumWindows gives top-level windows in Z order. Freeze their bounds with the
// desktop so hover selection cannot drift if an application moves afterward.
BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM lp)
{
 auto& scan = *reinterpret_cast<WindowScan*>(lp);
 DWORD pid = 0;
 GetWindowThreadProcessId(hwnd, &pid);
 if (pid == scan.self || hwnd == GetShellWindow() || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
 DWORD cloaked = 0;
 if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
 wchar_t cls[128]{};
 GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
 if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
     !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) return TRUE;
 RECT r{};
 if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) || IsRectEmpty(&r)) {
  if (!GetWindowRect(hwnd, &r)) return TRUE;
 }
 RECT clipped{};
 if (IntersectRect(&clipped, &r, &scan.desktop)) {
  scan.windows->emplace_back(clipped.left - scan.desktop.left, clipped.top - scan.desktop.top,
                             clipped.right - clipped.left, clipped.bottom - clipped.top);
 }
 return TRUE;
}

} // namespace

bool otherInstance(const wxString& command)
{
 g_mutex = CreateMutexW(nullptr, FALSE, L"Local\\SnipTool.SingleInstance");
 if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
 HWND other = FindWindowExW(HWND_MESSAGE, nullptr, kHostClass, nullptr);
 if (!other) other = FindWindowW(kHostClass, nullptr);   // a pre-wx snip.exe
 if (other && command != "tray") {
  DWORD pid = 0;
  GetWindowThreadProcessId(other, &pid);
  AllowSetForegroundWindow(pid);
  PostMessageW(other, command == "snip" ? WM_APP_SNIP : WM_APP_SHOW, 0, 0);
 }
 return true;
}

void startShortcuts(std::function<void()> snip, std::function<void()> show)
{
 g_snip = std::move(snip);
 g_show = std::move(show);
 WNDCLASSEXW wc{ sizeof(wc) };
 wc.lpfnWndProc = HostProc;
 wc.hInstance = GetModuleHandleW(nullptr);
 wc.lpszClassName = kHostClass;
 RegisterClassExW(&wc);
 g_host = CreateWindowExW(0, kHostClass, L"Snip", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
 if (!g_host) return;

 struct Hk { UINT mod, vk; const wchar_t* name; };
 const Hk hks[] = {
  { 0, VK_SNAPSHOT, L"PrtScn" },
  { MOD_WIN | MOD_SHIFT, 'S', L"Win+Shift+S" },
  { MOD_CONTROL | MOD_ALT, 'S', L"Ctrl+Alt+S" },
 };
 for (int i = 0; i < static_cast<int>(std::size(hks)); ++i) {
  bool available = RegisterHotKey(g_host, i + 1, hks[i].mod | MOD_NOREPEAT, hks[i].vk) != FALSE;
  if (!available && hks[i].mod == (MOD_WIN | MOD_SHIFT)) available = InstallCaptureKeyboardHook();
  if (available) {
   if (!g_names.empty()) g_names += " / ";
   g_names += hks[i].name;
  }
 }
}

void stopShortcuts()
{
 if (g_host) for (int id = 1; id <= 3; ++id) UnregisterHotKey(g_host, id);
 if (g_keyboardThread) {
  PostThreadMessageW(g_keyboardThreadId, WM_QUIT, 0, 0);
  WaitForSingleObject(g_keyboardThread, INFINITE);
  CloseHandle(g_keyboardThread);
 }
 g_keyboardThread = nullptr;
 g_keyboardThreadId = 0;
 g_keyboardHook = nullptr;
 g_shortcutHeld = false;
 if (g_host) DestroyWindow(g_host);
 g_host = nullptr;
 g_snip = g_show = nullptr;
}

wxString shortcutNames() { return g_names; }

namespace {
bool grabDesktop(DesktopShot& shot, wxString& error)
{
 const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
 const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
 BITMAPINFO bi{};
 bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
 bi.bmiHeader.biWidth = vw;
 bi.bmiHeader.biHeight = -vh;
 bi.bmiHeader.biPlanes = 1;
 bi.bmiHeader.biBitCount = 32;
 bi.bmiHeader.biCompression = BI_RGB;
 void* bits = nullptr;
 HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
 if (!dib) { error = "Cannot allocate a desktop image."; return false; }
 HDC screen = GetDC(nullptr);
 HDC mem = CreateCompatibleDC(screen);
 HGDIOBJ old = SelectObject(mem, dib);
 const bool ok = BitBlt(mem, 0, 0, vw, vh, screen, vx, vy, SRCCOPY | CAPTUREBLT) != FALSE;
 SelectObject(mem, old);
 DeleteDC(mem);
 ReleaseDC(nullptr, screen);
 GdiFlush();
 if (ok && shot.image.Create(vw, vh, false)) {
  const auto* src = static_cast<const uint32_t*>(bits);
  unsigned char* dst = shot.image.GetData();
  const size_t n = static_cast<size_t>(vw) * vh;
  for (size_t i = 0; i < n; ++i, dst += 3) {
   dst[0] = static_cast<unsigned char>(src[i] >> 16);
   dst[1] = static_cast<unsigned char>(src[i] >> 8);
   dst[2] = static_cast<unsigned char>(src[i]);
  }
 }
 DeleteObject(dib);
 if (!ok || !shot.image.IsOk()) { error = "Windows did not return a screen image."; return false; }

 for (const auto& m : monsel::Enumerate()) {
  const RECT& r = m.monitorRect;
  const wxRect geometry(r.left, r.top, r.right - r.left, r.bottom - r.top);
  shot.outputs.push_back({ geometry, wxRect(r.left - vx, r.top - vy, geometry.width, geometry.height) });
 }
 WindowScan scan{ &shot.windows, RECT{ vx, vy, vx + vw, vy + vh }, GetCurrentProcessId() };
 EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&scan));
 return true;
}
}

void capture(CaptureMode, std::function<void(DesktopShot shot, wxString error)> done)
{
 DesktopShot shot;
 wxString error;
 if (!grabDesktop(shot, error)) shot = DesktopShot{};
 done(std::move(shot), error);
}

void cancelCapture() {}

bool canSetupShortcut() { return false; }
void setupShortcut(std::function<void(const wxString&)>) {}

namespace {
class Tray : public wxTaskBarIcon {
public:
 explicit Tray(TrayActions actions) : actions_(std::move(actions))
 {
  Bind(wxEVT_TASKBAR_LEFT_UP, [this](wxTaskBarIconEvent&) { if (actions_.click) actions_.click(); });
  Bind(wxEVT_MENU, [this](wxCommandEvent& e) { if (actions_.chosen) actions_.chosen(e.GetId()); });
 }
protected:
 wxMenu* CreatePopupMenu() override
 {
  auto menu = new wxMenu;
  if (actions_.menu) fill(menu, actions_.menu());
  return menu;
 }
private:
 TrayActions actions_;
 static void fill(wxMenu* menu, const std::vector<TrayItem>& items)
 {
  for (const auto& item : items) {
   if (item.label.empty()) menu->AppendSeparator();
   else if (!item.submenu.empty()) {
    auto sub = new wxMenu;
    fill(sub, item.submenu);
    menu->AppendSubMenu(sub, item.label);
   } else if (item.checkable) menu->AppendCheckItem(item.id, item.label)->Check(item.checked);
   else menu->Append(item.id, item.label);
  }
 }
};
Tray* g_tray = nullptr;
}

bool startTray(TrayActions actions, const wxString& tooltip)
{
 g_tray = new Tray(std::move(actions));
 if (g_tray->SetIcon(wxBitmapBundle::FromIconBundle(appIcons()), tooltip)) return true;
 stopTray();
 return false;
}

void stopTray()
{
 if (!g_tray) return;
 g_tray->RemoveIcon();
 g_tray->Destroy();
 g_tray = nullptr;
}

void placeSurface(wxTopLevelWindow* window, const Output& output)
{
 if (window->GetRect() != output.geometry) window->SetSize(output.geometry);
}

// Puts both a "PNG" clipboard format (browsers / Electron apps prefer it) and a
// 24-bit CF_DIB (everything else) on the clipboard.
bool copyImage(wxWindow* owner, const wxImage& image, const wxMemoryBuffer& png)
{
 if (!image.IsOk()) return false;
 const int w = image.GetWidth(), h = image.GetHeight();
 const int stride = (w * 3 + 3) & ~3;
 const SIZE_T dibSize = sizeof(BITMAPINFOHEADER) + static_cast<SIZE_T>(stride) * h;
 HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dibSize);
 if (!hDib) return false;
 {
  BYTE* p = static_cast<BYTE*>(GlobalLock(hDib));
  auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(p);
  bih->biSize = sizeof(*bih);
  bih->biWidth = w;
  bih->biHeight = h;   // bottom-up
  bih->biPlanes = 1;
  bih->biBitCount = 24;
  bih->biCompression = BI_RGB;
  bih->biSizeImage = static_cast<DWORD>(stride) * h;
  BYTE* bits = p + sizeof(*bih);
  const unsigned char* rgb = image.GetData();
  for (int y = 0; y < h; ++y) {
   BYTE* row = bits + static_cast<SIZE_T>(h - 1 - y) * stride;
   const unsigned char* src = rgb + static_cast<size_t>(y) * w * 3;
   for (int x = 0; x < w; ++x) {
    row[x * 3 + 0] = src[x * 3 + 2];
    row[x * 3 + 1] = src[x * 3 + 1];
    row[x * 3 + 2] = src[x * 3 + 0];
   }
  }
  GlobalUnlock(hDib);
 }
 HGLOBAL hPng = nullptr;
 if (!png.IsEmpty() && (hPng = GlobalAlloc(GMEM_MOVEABLE, png.GetDataLen())) != nullptr) {
  memcpy(GlobalLock(hPng), png.GetData(), png.GetDataLen());
  GlobalUnlock(hPng);
 }

 HWND hwnd = static_cast<HWND>(owner->GetHWND());
 bool open = false;
 for (int i = 0; i < 10 && !(open = OpenClipboard(hwnd) != FALSE); ++i) Sleep(25);
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

bool supportsStartup() { return true; }

bool startupEnabled()
{
 HKEY k;
 if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
 const bool on = RegQueryValueExW(k, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
 RegCloseKey(k);
 return on;
}

void setStartup(bool on)
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

void instantWindow(wxTopLevelWindow* window)
{
 BOOL off = TRUE;
 DwmSetWindowAttribute(static_cast<HWND>(window->GetHWND()), DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof(off));
}

void bringToFront(wxTopLevelWindow* window)
{
 SetForegroundWindow(static_cast<HWND>(window->GetHWND()));
}

}
