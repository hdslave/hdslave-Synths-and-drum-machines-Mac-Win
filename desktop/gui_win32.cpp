#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <new>
#include <vector>

#include "desktop_wrapper.h"
#include "panel_ui.h"

#ifndef HDSLAVE_WIN32_CLASS_NAME
#define HDSLAVE_WIN32_CLASS_NAME L"HdslaveDesktopClapView"
#endif

namespace hdslave_desktop
{

struct PlatformGui
{
    DesktopPlugin* plugin = nullptr;
    HWND hwnd = nullptr;
    std::vector<uint32_t> framebuffer;
    int fbWidth = 0;
    int fbHeight = 0;
};

namespace
{
constexpr UINT_PTR kTimerId = 1;

LRESULT CALLBACK wndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* gui = reinterpret_cast<PlatformGui*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_TIMER:
        if (wParam == kTimerId)
            InvalidateRect (hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps {};
        HDC hdc = BeginPaint (hwnd, &ps);
        if (gui && gui->plugin && gui->plugin->ui)
        {
            RECT rc {};
            GetClientRect (hwnd, &rc);
            int w = std::max (1, int (rc.right - rc.left));
            int h = std::max (1, int (rc.bottom - rc.top));
            if (w != gui->fbWidth || h != gui->fbHeight)
            {
                gui->fbWidth = w;
                gui->fbHeight = h;
                gui->framebuffer.assign (size_t (w * h), 0xFF111318u);
            }

            using namespace std::chrono;
            uint64_t nowMs = uint64_t (duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count());
            gui->plugin->ui->render (gui->framebuffer.data(), w, h, nowMs);

            BITMAPINFO bmi {};
            bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = -h; // top-down DIB
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            StretchDIBits (hdc, 0, 0, w, h, 0, 0, w, h, gui->framebuffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint (hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            SetFocus (hwnd);
            SetCapture (hwnd);
            bool shift = (GetKeyState (VK_SHIFT) & 0x8000) != 0;
            gui->plugin->ui->onMouseDown (GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam), 0, shift, false);
            InvalidateRect (hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            bool shift = (GetKeyState (VK_SHIFT) & 0x8000) != 0;
            gui->plugin->ui->onMouseDown (GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam), 0, shift, true);
            InvalidateRect (hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_RBUTTONDOWN:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            SetFocus (hwnd);
            bool shift = (GetKeyState (VK_SHIFT) & 0x8000) != 0;
            gui->plugin->ui->onMouseDown (GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam), 1, shift, false);
            InvalidateRect (hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            bool shift = (GetKeyState (VK_SHIFT) & 0x8000) != 0;
            gui->plugin->ui->onMouseMove (GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam), shift);
            InvalidateRect (hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            ReleaseCapture();
            gui->plugin->ui->onMouseUp (GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam), 0);
            InvalidateRect (hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
            ScreenToClient (hwnd, &pt);
            float delta = float (GET_WHEEL_DELTA_WPARAM (wParam)) / float (WHEEL_DELTA);
            bool shift = (GetKeyState (VK_SHIFT) & 0x8000) != 0;
            gui->plugin->ui->onMouseWheel (pt.x, pt.y, delta, shift);
            InvalidateRect (hwnd, nullptr, FALSE);
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW (hwnd, msg, wParam, lParam);
}

void ensureWindowClassRegistered()
{
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc {};
    wc.cbSize = sizeof (wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW (nullptr);
    wc.hCursor = LoadCursorW (nullptr, IDC_ARROW);
    wc.lpszClassName = HDSLAVE_WIN32_CLASS_NAME;
    RegisterClassExW (&wc);
    registered = true;
}
} // namespace

const char* platformGuiApiName()
{
    return CLAP_WINDOW_API_WIN32;
}

PlatformGui* createPlatformGui (DesktopPlugin* plugin)
{
    ensureWindowClassRegistered();
    auto* g = new (std::nothrow) PlatformGui();
    if (!g) return nullptr;
    g->plugin = plugin;
    return g;
}

void destroyPlatformGui (PlatformGui* gui)
{
    if (!gui) return;
    if (gui->hwnd)
    {
        KillTimer (gui->hwnd, kTimerId);
        DestroyWindow (gui->hwnd);
        gui->hwnd = nullptr;
    }
    delete gui;
}

bool platformGuiSetParent (PlatformGui* gui, const clap_window_t* window)
{
    if (!gui || !window || !window->win32)
        return false;
    HWND parent = static_cast<HWND> (window->win32);
    if (!gui->hwnd)
    {
        gui->hwnd = CreateWindowExW (
            0, HDSLAVE_WIN32_CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, int (gui->plugin->guiWidth), int (gui->plugin->guiHeight),
            parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (!gui->hwnd) return false;
        SetWindowLongPtrW (gui->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (gui));
        SetTimer (gui->hwnd, kTimerId, 33, nullptr);
    }
    else
    {
        SetParent (gui->hwnd, parent);
    }
    return true;
}

bool platformGuiShow (PlatformGui* gui)
{
    if (!gui || !gui->hwnd) return false;
    ShowWindow (gui->hwnd, SW_SHOW);
    InvalidateRect (gui->hwnd, nullptr, FALSE);
    return true;
}

bool platformGuiHide (PlatformGui* gui)
{
    if (!gui || !gui->hwnd) return false;
    ShowWindow (gui->hwnd, SW_HIDE);
    return true;
}

bool platformGuiSetSize (PlatformGui* gui, uint32_t width, uint32_t height)
{
    if (!gui || !gui->hwnd) return false;
    SetWindowPos (gui->hwnd, nullptr, 0, 0, int (width), int (height), SWP_NOZORDER | SWP_NOMOVE);
    InvalidateRect (gui->hwnd, nullptr, FALSE);
    return true;
}

} // namespace hdslave_desktop

#endif // _WIN32
