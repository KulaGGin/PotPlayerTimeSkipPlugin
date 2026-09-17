#include "plugin/osd.hpp"

#include <windows.h>

#include <atomic>
#include <utility>

#include "diagnostics/log.hpp"
#include "plugin/player_window.hpp"

namespace plugin {

std::string ComposeOsdText(const OsdMessage& message) {
    switch (message.event) {
    case OsdEvent::kNoFileOpen:
        return "No file open";
    case OsdEvent::kNewMark:
        return "Skip: new mark";
    case OsdEvent::kMarkIgnored:
        return "Skip mark ignored (invalid range)";
    case OsdEvent::kMarkStart:
        return "Skip start " + core::FormatTimecodeShort(*message.startMs);
    case OsdEvent::kMarkEnd:
        return "Skip end " + core::FormatTimecodeShort(*message.endMs);
    case OsdEvent::kSaved:
        return "Skip " + core::FormatTimecodeShort(*message.startMs) + " – " +
               core::FormatTimecodeShort(*message.endMs) + " saved";
    case OsdEvent::kSaveFailed:
        return "Couldn't save mark";
    }
    return {};
}

namespace {

constexpr wchar_t kOsdClassName[] = L"PotPlayerTimeSkipOsd";
constexpr UINT_PTR kDismissTimerId = 1;
constexpr UINT kDismissMs = 1800;
constexpr int kPaddingX = 18;
constexpr int kPaddingY = 12;
// Clear of PotPlayer's own seek bar / control strip along the bottom edge,
// which the fixed corners of a toast could otherwise sit on top of.
constexpr int kBottomMarginPx = 80;
constexpr BYTE kAlpha = 215;

// Only ever touched from PTS-013's single hotkey pump thread — the same
// thread ShowOsdLive itself must be called from (see osd.hpp) — so this
// needs no lock, just the same "atomic, re-validated with IsWindow()"
// caching convention plugin/player_window.cpp's g_mainWindow already uses.
std::atomic<HWND> g_activeToast{nullptr};

// The text to paint is stashed on the window itself (GWLP_USERDATA) rather
// than captured by the wndproc, since a plain WNDPROC can't capture — freed
// on WM_NCDESTROY, the one message guaranteed to fire exactly once as the
// last step of every DestroyWindow, however it was triggered.
LRESULT CALLBACK OsdWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == kDismissTimerId) {
            KillTimer(hwnd, kDismissTimerId);
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        const HDC dc = BeginPaint(hwnd, &ps);

        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(dc, &client, background);
        DeleteObject(background);

        const auto* text = reinterpret_cast<const std::wstring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (text != nullptr) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT previousFont = static_cast<HFONT>(SelectObject(dc, font));
            RECT textRect = client;
            DrawTextW(dc, text->c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, previousFont);
            DeleteObject(font);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY: {
        const auto* text = reinterpret_cast<const std::wstring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        delete text;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// GetModuleHandleExW(..._FROM_ADDRESS...) against our own wndproc, the same
// self-module lookup proxy/dllmain.cpp uses to pin its module — simplest
// correct way for a DLL to name itself for RegisterClassExW without a
// stashed DllMain HMODULE anywhere in this file.
HINSTANCE SelfModuleHandle() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                        reinterpret_cast<LPCWSTR>(&OsdWndProc), &module);
    return module;
}

// Registered once for the process's lifetime (magic-static init is
// thread-safe, though ShowOsdLive is only ever called from one thread by
// contract) and never unregistered — same "lives as long as the process"
// choice as plugin/player_window.cpp's cached main-window handle.
ATOM RegisterOsdClassOnce() {
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &OsdWndProc;
        wc.hInstance = SelfModuleHandle();
        // IDC_ARROW expands via MAKEINTRESOURCE, which yields an ANSI LPSTR
        // when UNICODE isn't defined for this translation unit (it isn't,
        // like the rest of this codebase's windows.h usage) — reinterpret
        // as LPCWSTR rather than pull in the UNICODE macro project-wide.
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.lpszClassName = kOsdClassName;
        return RegisterClassExW(&wc);
    }();
    return atom;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

SIZE MeasureText(const std::wstring& text) {
    const HDC screenDc = GetDC(nullptr);
    HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT previousFont = static_cast<HFONT>(SelectObject(screenDc, font));
    SIZE extent{};
    GetTextExtentPoint32W(screenDc, text.c_str(), static_cast<int>(text.size()), &extent);
    SelectObject(screenDc, previousFont);
    DeleteObject(font);
    ReleaseDC(nullptr, screenDc);
    return extent;
}

}  // namespace

void ShowOsdLive(const std::string& text) {
    const HWND mainWindow = reinterpret_cast<HWND>(GetMainWindowHandle());
    if (mainWindow == nullptr) {
        LOG_WARN("osd: cannot show '{}', PotPlayer main window not found", text);
        return;
    }

    // A new message replaces whatever toast is already on screen rather
    // than stacking behind/beside it — the hotkeys fire in quick bursts
    // (Alt+A, Alt+[, Alt+]) well inside kDismissMs of each other.
    const HWND previous = g_activeToast.exchange(nullptr, std::memory_order_relaxed);
    if (previous != nullptr && IsWindow(previous)) {
        KillTimer(previous, kDismissTimerId);
        DestroyWindow(previous);
    }

    RegisterOsdClassOnce();

    std::wstring wide = Utf8ToWide(text);
    const SIZE extent = MeasureText(wide);
    const int width = extent.cx + kPaddingX * 2;
    const int height = extent.cy + kPaddingY * 2;

    RECT mainRect{};
    GetWindowRect(mainWindow, &mainRect);
    const int x = mainRect.left + ((mainRect.right - mainRect.left) - width) / 2;
    const int y = mainRect.bottom - kBottomMarginPx - height;

    // WS_EX_NOACTIVATE: never steals focus, not even on creation.
    // WS_EX_TRANSPARENT: click-through, informational only.
    // WS_EX_TOOLWINDOW: no taskbar entry, no Alt+Tab entry.
    // WS_EX_TOPMOST + WS_EX_LAYERED: stays above the video; the layered bit
    // also enables SetLayeredWindowAttributes' whole-window alpha blend
    // below.
    const HWND toast = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kOsdClassName, L"", WS_POPUP, x, y, width, height, nullptr, nullptr, SelfModuleHandle(), nullptr);
    if (toast == nullptr) {
        LOG_ERROR("osd: CreateWindowExW failed, gle={}", GetLastError());
        return;
    }

    SetWindowLongPtrW(toast, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new std::wstring(std::move(wide))));
    SetLayeredWindowAttributes(toast, 0, kAlpha, LWA_ALPHA);
    ShowWindow(toast, SW_SHOWNOACTIVATE);
    SetTimer(toast, kDismissTimerId, kDismissMs, nullptr);

    g_activeToast.store(toast, std::memory_order_relaxed);
}

}  // namespace plugin
