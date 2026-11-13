#pragma once

#include <functional>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

// 原生 WinUI 3 TextBox 提供系统编辑、输入法及辅助功能行为。
class SettingsWindow
{
public:
    SettingsWindow();
    ~SettingsWindow() = default;
    SettingsWindow(SettingsWindow const&) = delete;
    SettingsWindow& operator=(SettingsWindow const&) = delete;
    void Show();
    void Hide();
    std::function<void()> OnSaved;

private:
    void BuildUi();
    void Save();
    void ResizeToContent();
    void CenterOnScreen();
    void ApplyTheme();
    winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
    HWND m_hwnd{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Grid m_root{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBox m_addressField{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::InfoBar m_errorInfo{ nullptr };
};
