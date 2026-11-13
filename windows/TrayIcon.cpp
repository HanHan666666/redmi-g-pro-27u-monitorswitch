#include "pch.h"

#include "TrayIcon.h"

#include <algorithm>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace
{
    UINT g_taskbarCreatedMessage = 0;
    ULONG_PTR g_gdiplusToken = 0;

    bool EnsureGdiplus()
    {
        if (g_gdiplusToken != 0)
        {
            return true;
        }
        Gdiplus::GdiplusStartupInput input;
        return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    }

    void AddRoundedRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF const& rect, float radius)
    {
        float diameter = radius * 2.0f;
        path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
        path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
        path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
        path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
        path.CloseFigure();
    }

    HBITMAP CopyToDib(Gdiplus::Bitmap& bitmap, int size)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size;
        info.bmiHeader.biHeight = -size;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP handle = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (handle == nullptr)
        {
            return nullptr;
        }

        Gdiplus::Rect rect(0, 0, size, size);
        Gdiplus::BitmapData data{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        {
            DeleteObject(handle);
            return nullptr;
        }
        for (int y = 0; y < size; ++y)
        {
            memcpy(static_cast<BYTE*>(bits) + static_cast<size_t>(y) * size * 4,
                static_cast<BYTE*>(data.Scan0) + static_cast<size_t>(y) * data.Stride, static_cast<size_t>(size) * 4);
        }
        bitmap.UnlockBits(&data);
        return handle;
    }
}

TrayIcon::~TrayIcon()
{
    Remove();
}

bool TrayIcon::Install(std::function<void()> onActivate, std::function<void(int)> onCommand)
{
    m_onActivate = std::move(onActivate);
    m_onCommand = std::move(onCommand);

    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &TrayIcon::WndProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kTrayWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }
        classRegistered = true;
    }

    if (g_taskbarCreatedMessage == 0)
    {
        g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    }

    m_window = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayWindowClass, L"MonitorSwitch", 0, 0, 0, 0, 0, nullptr, nullptr,
        GetModuleHandleW(nullptr), this);
    if (m_window == nullptr)
    {
        return false;
    }
    SetWindowLongPtrW(m_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    UpdateTheme();
    UpdateIcon();
    return m_installed;
}

void TrayIcon::Remove()
{
    if (m_window != nullptr && m_installed)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_window;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        m_installed = false;
    }
    if (m_icon != nullptr)
    {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
    if (m_window != nullptr)
    {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void TrayIcon::SetVisualState(TrayVisualState state, std::wstring const& tooltip)
{
    if (m_state == state && m_tooltip == tooltip) return;
    m_state = state;
    m_tooltip = tooltip;
    UpdateIcon();
}

bool TrayIcon::GetIconRect(RECT& rect) const
{
    if (m_window == nullptr || !m_installed)
    {
        return false;
    }
    NOTIFYICONIDENTIFIER identifier{};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = m_window;
    identifier.uID = kTrayIconId;
    return SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect)) ? TRUE : FALSE;
}

void TrayIcon::UpdateTheme()
{
    DWORD light = 1;
    DWORD size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
    {
        size = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        {
            light = 1;
        }
    }
    m_darkTaskbar = light == 0;
}

void TrayIcon::UpdateIcon()
{
    if (m_window == nullptr)
    {
        return;
    }

    HICON icon = RenderIcon();
    bool ownsIcon = icon != nullptr;
    if (!ownsIcon)
    {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = m_window;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = icon;
    wcsncpy_s(data.szTip, m_tooltip.c_str(), _TRUNCATE);

    BOOL ok = Shell_NotifyIconW(m_installed ? NIM_MODIFY : NIM_ADD, &data);
    if (ok != FALSE && !m_installed)
    {
        m_installed = true;
    }

    if (ownsIcon && icon != nullptr)
    {
        // 系统只保存图标句柄的副本，本地句柄用完即销毁
        DestroyIcon(icon);
    }
}

HICON TrayIcon::RenderIcon() const
{
    if (!EnsureGdiplus())
    {
        return nullptr;
    }

    int size = std::max(32, GetSystemMetrics(SM_CXSMICON) * 2);
    size = std::min(size, 64);
    const float s = static_cast<float>(size);

    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        BYTE tone = m_darkTaskbar ? 0xFF : 0x1B;
        Gdiplus::Color color(255, tone, tone, tone);
        Gdiplus::Pen pen(color, std::max(1.6f, s / 16.0f));
        pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::SolidBrush brush(color);

        if (m_state == TrayVisualState::Online || m_state == TrayVisualState::Offline)
        {
            Gdiplus::GraphicsPath path;
            AddRoundedRect(path, Gdiplus::RectF(s * 0.14f, s * 0.18f, s * 0.72f, s * 0.48f), s * 0.07f);
            graphics.DrawPath(&pen, &path);
            graphics.DrawLine(&pen, s * 0.5f, s * 0.66f, s * 0.5f, s * 0.79f);
            graphics.DrawLine(&pen, s * 0.31f, s * 0.83f, s * 0.69f, s * 0.83f);
            if (m_state == TrayVisualState::Offline)
            {
                graphics.DrawLine(&pen, s * 0.19f, s * 0.84f, s * 0.84f, s * 0.19f);
            }
        }
        else
        {
            graphics.DrawEllipse(&pen, s * 0.12f, s * 0.12f, s * 0.76f, s * 0.76f);

            Gdiplus::FontFamily family(L"Segoe UI");
            Gdiplus::Font font(&family, s * 0.48f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF textRect(0.0f, -s * 0.02f, s, s);
            graphics.DrawString(m_state == TrayVisualState::AdbMissing ? L"!" : L"?", -1, &font, textRect, &format, &brush);
        }
    }

    HBITMAP colorBitmap = CopyToDib(bitmap, size);
    if (colorBitmap == nullptr)
    {
        return nullptr;
    }
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, nullptr);
    if (maskBitmap == nullptr)
    {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = maskBitmap;
    info.hbmColor = colorBitmap;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

LRESULT CALLBACK TrayIcon::WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr)
    {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayIcon::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == kTrayCallbackMessage)
    {
        OnTrayEvent(LOWORD(lParam));
        return 0;
    }
    if (message == kTrayShowPanelMessage)
    {
        if (m_onActivate)
        {
            m_onActivate();
        }
        return 0;
    }
    if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_DWMCOLORIZATIONCOLORCHANGED)
    {
        UpdateTheme();
        UpdateIcon();
        return 0;
    }
    if (g_taskbarCreatedMessage != 0 && message == g_taskbarCreatedMessage)
    {
        m_installed = false;
        UpdateIcon();
        return 0;
    }
    return DefWindowProcW(m_window, message, wParam, lParam);
}

void TrayIcon::OnTrayEvent(UINT event)
{
    switch (event)
    {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case NIN_SELECT:
    case NIN_KEYSELECT:
        if (m_onActivate)
        {
            m_onActivate();
        }
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;
    default:
        break;
    }
}

void TrayIcon::ShowContextMenu()
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }
    AppendMenuW(menu, MF_STRING, 1, L"打开面板");
    AppendMenuW(menu, MF_STRING, 2, L"刷新");
    AppendMenuW(menu, MF_STRING, 3, L"ADB 设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"退出");

    POINT cursor{};
    GetCursorPos(&cursor);
    // 必须先让托盘窗口前台化，否则菜单点外面不会关闭
    SetForegroundWindow(m_window);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, m_window, nullptr);
    DestroyMenu(menu);
    PostMessageW(m_window, WM_NULL, 0, 0);

    if (command != 0 && m_onCommand)
    {
        m_onCommand(command);
    }
}
