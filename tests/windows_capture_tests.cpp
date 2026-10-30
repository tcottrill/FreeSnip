// Exercise the real native overlay and clipboard path in a standalone process.
#include "../src/main.cpp"
#include <cstdio>
#include <fstream>

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_inst = GetModuleHandleW(nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    WNDCLASSW wc{};
    wc.hInstance = g_inst;
    wc.lpfnWndProc = OverlayProc;
    wc.lpszClassName = kOverlayClass;
    RegisterClassW(&wc);
    wc.lpfnWndProc = PreviewProc;
    wc.lpszClassName = kPreviewClass;
    RegisterClassW(&wc);
    g_host = CreateWindowW(L"STATIC", L"Test host", WS_POPUP, 0, 0, 1, 1,
                           nullptr, nullptr, g_inst, nullptr);
    const RECT work = monsel::Primary().workRect;
    HWND target = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"Capture test target",
        WS_POPUP | WS_VISIBLE, work.left + 80, work.top + 100, 360, 240,
        nullptr, nullptr, g_inst, nullptr);
    HWND front = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"Front test window",
        WS_POPUP | WS_VISIBLE, work.left + 250, work.top + 200, 120, 80,
        nullptr, nullptr, g_inst, nullptr);
    HWND hidden = CreateWindowExW(0, L"STATIC", L"Hidden test window",
        WS_POPUP, work.left + 80, work.top + 100, 361, 241,
        nullptr, nullptr, g_inst, nullptr);
    UpdateWindow(target);
    DwmFlush();
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    RegisterHotkeys();
    check(g_hotkeys.find(L"Win+Shift+S") != std::wstring::npos,
          "Win+Shift+S is available for capture on this desktop");
    INPUT keys[7]{};
    const WORD virtualKeys[] = { VK_LWIN, VK_LSHIFT, 'S', 'S', 'S', VK_LSHIFT, VK_LWIN };
    for (int i = 0; i < 7; ++i) {
        keys[i].type = INPUT_KEYBOARD;
        keys[i].ki.wVk = virtualKeys[i];
        if (i >= 4) keys[i].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    check(SendInput(7, keys, sizeof(INPUT)) == 7, "send Win+Shift+S with held-key repeat");
    int shortcutMessages = 0;
    const ULONGLONG until = GetTickCount64() + 300;
    while (GetTickCount64() < until) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.hwnd == g_host && (msg.message == WM_APP_SNIP ||
                (msg.message == WM_HOTKEY && msg.wParam == 2))) ++shortcutMessages;
            else { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        Sleep(1);
    }
    check(shortcutMessages == 1, "Win+Shift+S requests one snip without repeating");
    UnregisterCaptureHotkeys();
    HostProc(g_host, WM_APP + 3, 0, 0);
    check(g_preview && IsWindowVisible(g_preview) && !g_overlay && !g_snipPending,
          "normal activation opens the GUI without starting capture");
    check(g_pv.btnNew && IsWindowEnabled(g_pv.btnNew) &&
          g_pv.btnWindow && IsWindowEnabled(g_pv.btnWindow) &&
          !IsWindowEnabled(g_pv.btnCopy) && !IsWindowEnabled(g_pv.btnSave),
          "empty GUI enables capture and disables Copy and Save");
    if (g_preview) {
        SendMessageW(g_preview, WM_COMMAND, ID_COPY, 0);
        SendMessageW(g_preview, WM_COMMAND, ID_SAVE, 0);
        wchar_t status[128]{};
        GetWindowTextW(g_pv.status, status, 128);
        check(!wcscmp(status, L"Ready"), "empty-state accelerators leave Ready status unchanged");
        UpdateWindow(g_preview);
        DwmFlush();
        RECT client{};
        GetClientRect(g_preview, &client);
        uint32_t* pixels = nullptr;
        HBITMAP bitmap = CreateDib32(client.right, client.bottom, &pixels);
        HDC src = GetDC(g_preview), dst = CreateCompatibleDC(src);
        HGDIOBJ old = SelectObject(dst, bitmap);
        BitBlt(dst, 0, 0, client.right, client.bottom, src, 0, 0, SRCCOPY);
        GdiFlush();
        Image startup;
        startup.w = client.right; startup.h = client.bottom;
        startup.px.assign(pixels, pixels + static_cast<size_t>(startup.w) * startup.h);
        if (EncodePng(startup)) {
            std::ofstream png("startup-ui.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(startup.png.data()), startup.png.size());
        }
        SelectObject(dst, old);
        DeleteObject(bitmap); DeleteDC(dst); ReleaseDC(g_preview, src);
        ShowWindow(g_preview, SW_MINIMIZE);
        HostProc(g_host, WM_APP + 3, 0, 0);
        check(!IsIconic(g_preview), "normal activation restores minimized GUI");
        ShowWindow(g_preview, SW_HIDE);
    }
    check(BeginOverlay(), "create frozen desktop");
    if (g_overlay) {
        HostProc(g_host, WM_APP + 3, 0, 0);
        check(!g_preview || !IsWindowVisible(g_preview), "activation does not cover an active snip");
        SendMessageW(g_overlay, WM_KEYDOWN, 'W', 0);
        SelectWindowAt({ work.left + 260 - g_ov.vx, work.top + 210 - g_ov.vy });
        check(g_ov.windowRect.right - g_ov.windowRect.left == 120 &&
              g_ov.windowRect.bottom - g_ov.windowRect.top == 80,
              "overlapping windows select the topmost window");
        bool foundHidden = false;
        for (const RECT& r : g_ov.windows)
            if (r.right - r.left == 361 && r.bottom - r.top == 241) foundHidden = true;
        check(!foundHidden, "hidden window excluded");
        MoveWindow(front, work.left + 500, work.top + 300, 180, 100, FALSE);
        SelectWindowAt({ work.left + 260 - g_ov.vx, work.top + 210 - g_ov.vy });
        check(g_ov.windowRect.right - g_ov.windowRect.left == 120,
              "window selection stays aligned with frozen snapshot after window moves");
        // Deterministic frozen pixels ensure capture uses the stored snapshot.
        std::fill_n(g_ov.scrBits, static_cast<size_t>(g_ov.vw) * g_ov.vh, 0x00123456u);
        const int x = work.left + 150 - g_ov.vx, y = work.top + 150 - g_ov.vy;
        SendMessageW(g_overlay, WM_KEYDOWN, 'W', 0);
        SendMessageW(g_overlay, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        SendMessageW(g_overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(g_overlay, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        check(!g_overlay && g_img.w == 360 && g_img.h == 240,
              "W then click captures the window bounds");
        check(!g_img.px.empty() && g_img.px.front() == 0xFF123456u &&
              g_img.px.back() == 0xFF123456u, "window copies frozen pixels");
        check(!g_img.png.empty() && g_img.png[0] == 0x89, "window PNG encoded");
        check(IsWindowEnabled(g_pv.btnCopy) && IsWindowEnabled(g_pv.btnSave),
              "completed capture enables Copy and Save");
        check(IsClipboardFormatAvailable(CF_DIB) &&
              IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")),
              "window publishes bitmap and PNG clipboard formats");
        if (g_overlay) DestroyWindow(g_overlay);
    }
    DestroyWindow(front);
    DestroyWindow(hidden);
    if (g_preview) ShowWindow(g_preview, SW_HIDE);
    check(BeginOverlay(), "create rectangle regression overlay");
    if (g_overlay) {
        SendMessageW(g_overlay, WM_KEYDOWN, 'W', 0);
        SendMessageW(g_overlay, WM_KEYDOWN, 'R', 0);
        SendMessageW(g_overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(60, 70));
        SendMessageW(g_overlay, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(159, 119));
        SendMessageW(g_overlay, WM_LBUTTONUP, 0, MAKELPARAM(159, 119));
        check(!g_overlay && g_img.w == 100 && g_img.h == 50,
              "R restores arbitrary rectangle capture");
        if (g_overlay) DestroyWindow(g_overlay);
    }
    if (g_preview) DestroyWindow(g_preview);
    check(BeginOverlay(), "create mid-drag mode-switch overlay");
    if (g_overlay) {
        const int x = work.left + 150 - g_ov.vx, y = work.top + 150 - g_ov.vy;
        SendMessageW(g_overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(g_overlay, WM_KEYDOWN, 'W', 0);
        SendMessageW(g_overlay, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        check(g_overlay != nullptr, "switching mode during a held drag requires a fresh click");
        if (g_overlay) DestroyWindow(g_overlay);
    }
    if (g_preview) DestroyWindow(g_preview);
    const int previousWidth = g_img.w;
    check(BeginOverlay(), "create cancel overlay");
    if (g_overlay) {
        SendMessageW(g_overlay, WM_KEYDOWN, 'W', 0);
        SendMessageW(g_overlay, WM_KEYDOWN, VK_ESCAPE, 0);
        check(!g_overlay && g_img.w == previousWidth, "window cancel preserves previous image");
    }
    check(BeginOverlay(), "create monitor overlay");
    if (g_overlay) {
        POINT cursor{};
        GetCursorPos(&cursor);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
        SendMessageW(g_overlay, WM_KEYDOWN, VK_RETURN, 0);
        check(!g_overlay && g_img.w == mi.rcMonitor.right - mi.rcMonitor.left &&
              g_img.h == mi.rcMonitor.bottom - mi.rcMonitor.top,
              "Enter still captures the whole monitor in window mode");
    }
    if (g_preview) DestroyWindow(g_preview);
    DestroyWindow(target);
    DestroyWindow(g_host);
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    return failures ? 1 : 0;
}
