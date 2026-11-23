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
#include <cmath>
#include <new>

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
    int scale = 1;
    bool trackingMouse = false;
};

namespace
{
constexpr UINT_PTR kTimerId = 1;
constexpr LPARAM kTouchSignatureMask = 0xFFFFFF00L;
constexpr LPARAM kTouchSignature = 0xFF515700L;

uint32_t currentMods()
{
    uint32_t m = 0;
    if (GetKeyState (VK_SHIFT) & 0x8000) m |= kModShift;
    if (GetKeyState (VK_MENU) & 0x8000) m |= kModAlt;
    if (GetKeyState (VK_CONTROL) & 0x8000) m |= kModCmd;
    if ((GetMessageExtraInfo() & kTouchSignatureMask) == kTouchSignature) m |= kModTouch;
    return m;
}

int dpiScaleForWindow (HWND hwnd)
{
    using GetDpiFn = UINT (WINAPI*) (HWND);
    static GetDpiFn fn = reinterpret_cast<GetDpiFn> (GetProcAddress (GetModuleHandleW (L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = fn ? fn (hwnd) : 96u;
    return (dpi >= 144u) ? 2 : 1;
}

void syncAndPump (PlatformGui* gui)
{
    if (!gui || !gui->hwnd || !gui->plugin || !gui->plugin->ui) return;
    RECT rc {};
    GetClientRect (gui->hwnd, &rc);
    const int pw = std::max (1, int (rc.right - rc.left));
    const int ph = std::max (1, int (rc.bottom - rc.top));
    gui->scale = dpiScaleForWindow (gui->hwnd);
    const int lw = std::max (320, pw / gui->scale);
    const int lh = std::max (240, ph / gui->scale);
    gui->plugin->ui->setSize (lw, lh, gui->scale);

    const Rect dirty = gui->plugin->ui->update (nowMs());
    if (!dirty.empty())
    {
        RECT inv {
            dirty.x * gui->scale,
            dirty.y * gui->scale,
            (dirty.x + dirty.w) * gui->scale,
            (dirty.y + dirty.h) * gui->scale
        };
        InvalidateRect (gui->hwnd, &inv, FALSE);
    }
}

LRESULT CALLBACK wndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* gui = reinterpret_cast<PlatformGui*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_TIMER:
        if (wParam == kTimerId)
            syncAndPump (gui);
        return 0;

    case WM_SIZE:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            gui->plugin->ui->invalidateAll();
            syncAndPump (gui);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps {};
        HDC hdc = BeginPaint (hwnd, &ps);
        if (gui && gui->plugin && gui->plugin->ui)
        {
            if (gui->plugin->ui->physWidth() <= 0)
                syncAndPump (gui);

            const uint32_t* px = gui->plugin->ui->pixels();
            const int pw = gui->plugin->ui->physWidth();
            const int ph = gui->plugin->ui->physHeight();
            if (px && pw > 0 && ph > 0)
            {
                BITMAPINFO bmi {};
                bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = pw;
                bmi.bmiHeader.biHeight = -ph; // top-down DIB
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                RECT rc {};
                GetClientRect (hwnd, &rc);
                StretchDIBits (hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                               0, 0, pw, ph, px, &bmi, DIB_RGB_COLORS, SRCCOPY);
            }
        }
        EndPaint (hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            SetFocus (hwnd);
            SetCapture (hwnd);
            const float lx = float (GET_X_LPARAM (lParam)) / float (gui->scale);
            const float ly = float (GET_Y_LPARAM (lParam)) / float (gui->scale);
            gui->plugin->ui->mouseDown (lx, ly, 0, currentMods(), 1);
            syncAndPump (gui);
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            const float lx = float (GET_X_LPARAM (lParam)) / float (gui->scale);
            const float ly = float (GET_Y_LPARAM (lParam)) / float (gui->scale);
            gui->plugin->ui->mouseDown (lx, ly, 0, currentMods(), 2);
            syncAndPump (gui);
        }
        return 0;

    case WM_RBUTTONDOWN:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            SetFocus (hwnd);
            const float lx = float (GET_X_LPARAM (lParam)) / float (gui->scale);
            const float ly = float (GET_Y_LPARAM (lParam)) / float (gui->scale);
            gui->plugin->ui->mouseDown (lx, ly, 1, currentMods(), 1);
            syncAndPump (gui);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            if (!gui->trackingMouse)
            {
                TRACKMOUSEEVENT tme { sizeof (tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent (&tme);
                gui->trackingMouse = true;
            }
            const float lx = float (GET_X_LPARAM (lParam)) / float (gui->scale);
            const float ly = float (GET_Y_LPARAM (lParam)) / float (gui->scale);
            gui->plugin->ui->mouseMove (lx, ly, currentMods());
            syncAndPump (gui);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            gui->trackingMouse = false;
            gui->plugin->ui->mouseExit();
            syncAndPump (gui);
        }
        return 0;

    case WM_LBUTTONUP:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            ReleaseCapture();
            const float lx = float (GET_X_LPARAM (lParam)) / float (gui->scale);
            const float ly = float (GET_Y_LPARAM (lParam)) / float (gui->scale);
            gui->plugin->ui->mouseUp (lx, ly, 0, currentMods());
            syncAndPump (gui);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            POINT pt { GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam) };
            ScreenToClient (hwnd, &pt);
            const float lx = float (pt.x) / float (gui->scale);
            const float ly = float (pt.y) / float (gui->scale);
            const float delta = float (GET_WHEEL_DELTA_WPARAM (wParam)) / float (WHEEL_DELTA);
            gui->plugin->ui->wheel (lx, ly, 0.0f, delta, false, currentMods());
            syncAndPump (gui);
        }
        return 0;

    case WM_KEYDOWN:
        if (gui && gui->plugin && gui->plugin->ui)
        {
            int k = -1;
            switch (wParam)
            {
                case VK_LEFT: k = 0; break;
                case VK_RIGHT: k = 1; break;
                case VK_UP: k = 2; break;
                case VK_DOWN: k = 3; break;
                case VK_RETURN: case VK_SPACE: k = 4; break;
                case VK_ESCAPE: k = 5; break;
                default: break;
            }
            if (k >= 0 && gui->plugin->ui->key (k))
            {
                syncAndPump (gui);
                return 0;
            }
        }
        break;

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
        gui->scale = dpiScaleForWindow (parent);
        gui->hwnd = CreateWindowExW (
            0, HDSLAVE_WIN32_CLASS_NAME, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, int (gui->plugin->guiWidth) * gui->scale, int (gui->plugin->guiHeight) * gui->scale,
            parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (!gui->hwnd) return false;
        SetWindowLongPtrW (gui->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (gui));
        SetTimer (gui->hwnd, kTimerId, 16, nullptr); // ~60fps
        syncAndPump (gui);
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
    if (gui->plugin && gui->plugin->ui) gui->plugin->ui->invalidateAll();
    syncAndPump (gui);
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
    gui->scale = dpiScaleForWindow (gui->hwnd);
    SetWindowPos (gui->hwnd, nullptr, 0, 0, int (width) * gui->scale, int (height) * gui->scale, SWP_NOZORDER | SWP_NOMOVE);
    syncAndPump (gui);
    return true;
}

} // namespace hdslave_desktop

#endif // _WIN32
