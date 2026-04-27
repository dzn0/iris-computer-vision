#pragma once
#include <windows.h>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

struct OverlayBox { int x, y, w, h; };

namespace Overlay {

static const COLORREF KEY_COLOR   = RGB(0, 0, 1);   // near-black = transparent colorkey
static const UINT     TIMER_ID    = 1;
static const UINT     TIMER_MS    = 50;             // repaint tick (20 fps)

static HWND              s_hwnd{};
static std::mutex        s_mutex;
static std::vector<OverlayBox> s_boxes;            // detection boxes (red)
static std::thread       s_thread;
static std::atomic<bool> s_running{ false };
static std::atomic<bool> s_visible{ false };       // tracks current visibility to avoid redundant SetWindowPos

// ROI indicator (cyan, temporary)
static OverlayBox        s_roi_box{};
static std::atomic<bool> s_roi_show{ false };
static std::chrono::steady_clock::time_point s_roi_expire;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE:
        SetTimer(hwnd, TIMER_ID, TIMER_MS, NULL);
        return 0;

    case WM_TIMER: {
        // Auto-hide ROI indicator when expired
        if (s_roi_show && std::chrono::steady_clock::now() >= s_roi_expire)
            s_roi_show = false;
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fill with transparency key (makes entire window transparent)
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(KEY_COLOR);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // ── Detection boxes (red) ─────────────────────────────────────────
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (!s_boxes.empty()) {
                HPEN pen      = CreatePen(PS_SOLID, 2, RGB(255, 55, 55));
                HPEN oldPen   = (HPEN)SelectObject(hdc, pen);
                HBRUSH oldBr  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                for (const auto& b : s_boxes)
                    Rectangle(hdc, b.x, b.y, b.x + b.w, b.y + b.h);
                SelectObject(hdc, oldBr);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
            }
        }

        // ── ROI indicator (cyan, temporary) ──────────────────────────────
        if (s_roi_show) {
            OverlayBox b;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                b = s_roi_box;
            }
            HPEN pen    = CreatePen(PS_SOLID, 3, RGB(180, 0, 255));
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, b.x, b.y, b.x + b.w, b.y + b.h);
            // Corner ticks for clearer ROI boundary
            int tick = 12;
            // Top-left
            MoveToEx(hdc, b.x,        b.y + tick, NULL); LineTo(hdc, b.x,        b.y       );
            LineTo  (hdc, b.x + tick, b.y);
            // Top-right
            MoveToEx(hdc, b.x+b.w-tick, b.y,       NULL); LineTo(hdc, b.x+b.w, b.y);
            LineTo  (hdc, b.x+b.w,     b.y + tick);
            // Bottom-left
            MoveToEx(hdc, b.x,        b.y+b.h-tick, NULL); LineTo(hdc, b.x,        b.y+b.h  );
            LineTo  (hdc, b.x + tick, b.y+b.h);
            // Bottom-right
            MoveToEx(hdc, b.x+b.w-tick, b.y+b.h,    NULL); LineTo(hdc, b.x+b.w, b.y+b.h);
            LineTo  (hdc, b.x+b.w,     b.y+b.h-tick);
            SelectObject(hdc, oldBr);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DISPLAYCHANGE: {
        // Screen resolution or fullscreen mode changed — resize overlay and reassert topmost
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_NOACTIVATE);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        s_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ThreadProc() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WindowsInputHost";
    wc.hbrBackground = NULL;
    if (!RegisterClassExW(&wc)) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    s_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"WindowsInputHost", L"",
        WS_POPUP,
        0, 0, sw, sh,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );
    if (!s_hwnd) return;

    SetLayeredWindowAttributes(s_hwnd, KEY_COLOR, 0, LWA_COLORKEY);

    MSG msg;
    while (s_running && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void Init() {
    if (s_running) return;  // já inicializado
    s_running = true;
    s_visible.store(false);
    s_thread  = std::thread(ThreadProc);
}

static bool IsROIShowing() { return s_roi_show.load(); }

static void SetVisible(bool v) {
    if (!s_hwnd) return;
    if (!v && s_roi_show) return;
    if (s_visible.load() == v) return;   // no state change — skip expensive window op
    s_visible.store(v);
    if (v) {
        // Reassert TOPMOST on show — recovers Z-order after fullscreen transitions
        SetWindowPos(s_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else {
        ShowWindow(s_hwnd, SW_HIDE);
    }
}

static void Update(const std::vector<OverlayBox>& boxes) {
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_boxes = boxes;
    }
    // Trigger immediate repaint — don't wait up to 50ms for WM_TIMER
    if (s_hwnd) InvalidateRect(s_hwnd, NULL, FALSE);
}

// Show ROI rectangle (cyan) for ~5 seconds
static void ShowROI(int x, int y, int w, int h) {
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_roi_box = { x, y, w, h };
    }
    s_roi_expire = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    s_roi_show   = true;
    if (s_hwnd) ShowWindow(s_hwnd, SW_SHOWNOACTIVATE);
}

// Destrói a window completamente (usar no dormant — Vanguard enumera janelas hidden)
static void Destroy() {
    s_running = false;
    s_visible.store(false);
    s_roi_show.store(false);
    if (s_hwnd) PostMessageW(s_hwnd, WM_QUIT, 0, 0);
    if (s_thread.joinable()) s_thread.join();
    s_hwnd = NULL;
}

static void Shutdown() {
    Destroy();
}

} // namespace Overlay
