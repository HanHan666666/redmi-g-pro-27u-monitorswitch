#pragma once

#include <windows.h>

#include <functional>
#include <optional>
#include <string>

inline constexpr wchar_t kTrayWindowClass[] = L"MonitorSwitchTrayWindow";
inline constexpr UINT kTrayShowPanelMessage = WM_APP + 100;
inline constexpr UINT kTrayCallbackMessage = WM_APP + 101;
inline constexpr UINT kTrayIconId = 1;

enum class TrayVisualState
{
    Online,
    Offline,
    NotConfigured,
    AdbMissing,
};

// 桌面右下角托盘图标：Shell_NotifyIcon + 一个隐藏消息窗口。
// 图标用 GDI+ 按当前任务栏明暗实时绘制（display / display-slash / ? / !）。
class TrayIcon
{
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(TrayIcon const&) = delete;
    TrayIcon& operator=(TrayIcon const&) = delete;

    bool Install(std::function<void()> onActivate, std::function<void(int)> onCommand);
    void Remove();
    void SetVisualState(TrayVisualState state, std::wstring const& tooltip);
    bool GetIconRect(RECT& rect) const;

private:
    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnTrayEvent(UINT event);
    void ShowContextMenu();
    void UpdateIcon();
    void UpdateTheme();
    HICON RenderIcon() const;

    HWND m_window{ nullptr };
    HICON m_icon{ nullptr };
    TrayVisualState m_state{ TrayVisualState::NotConfigured };
    std::wstring m_tooltip{ L"MonitorSwitch" };
    bool m_installed{ false };
    bool m_darkTaskbar{ false };
    std::function<void()> m_onActivate;
    std::function<void(int)> m_onCommand;
};
