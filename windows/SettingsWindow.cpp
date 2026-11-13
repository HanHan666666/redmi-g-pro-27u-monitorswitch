#include "pch.h"
#include "SettingsWindow.h"

#include <cmath>
#include <limits>
#include <microsoft.ui.xaml.window.h>

#include "Config.h"
#include "UiTheme.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Windowing;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;

SettingsWindow::SettingsWindow()
{
    m_window = Window();
    m_window.SystemBackdrop(DesktopAcrylicBackdrop());
    check_hresult(m_window.as<IWindowNative>()->get_WindowHandle(&m_hwnd));
    m_appWindow = m_window.AppWindow();
    m_appWindow.Title(L"ADB 设置 · MonitorSwitch");
    if (auto presenter = m_appWindow.Presenter().try_as<OverlappedPresenter>())
    {
        presenter.IsResizable(false);
        presenter.IsMaximizable(false);
        presenter.IsMinimizable(false);
    }
    m_appWindow.Closing([this](auto const&, AppWindowClosingEventArgs const& args) {
        args.Cancel(true);
        Hide();
    });
    BuildUi();
    ApplyTheme();
    m_root.ActualThemeChanged([this](auto const&, auto const&) { ApplyTheme(); });
    m_root.Loaded([this](auto const&, auto const&) {
        ResizeToContent();
        m_addressField.Focus(FocusState::Programmatic);
        m_addressField.SelectAll();
        if (auto xamlRoot = m_root.XamlRoot())
        {
            xamlRoot.Changed([this](auto const&, auto const&) { ResizeToContent(); });
        }
    });
}

void SettingsWindow::BuildUi()
{
    m_root = Grid();
    m_root.Padding(Thickness{ 24, 24, 24, 24 });
    auto content = StackPanel();
    content.Spacing(20);
    m_root.Children().Append(content);

    auto heading = StackPanel();
    heading.Spacing(8);
    TextBlock title;
    title.Text(L"连接显示器");
    title.FontSize(24);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    heading.Children().Append(title);
    TextBlock description;
    description.Text(L"在显示器上开启「网络 ADB 调试」，并确保电脑与显示器连接同一局域网。");
    description.FontSize(14);
    description.TextWrapping(TextWrapping::Wrap);
    heading.Children().Append(description);
    content.Children().Append(heading);

    auto inputSection = StackPanel();
    inputSection.Spacing(8);
    m_addressField = TextBox();
    m_addressField.Header(box_value(L"设备地址"));
    m_addressField.PlaceholderText(L"例如 192.168.1.100:5555");
    m_addressField.MaxLength(64);
    m_addressField.IsSpellCheckEnabled(false);
    m_addressField.IsTextPredictionEnabled(false);
    m_addressField.HorizontalAlignment(HorizontalAlignment::Stretch);
    Automation::AutomationProperties::SetName(m_addressField, L"显示器 ADB 地址");
    m_addressField.TextChanged([this](auto const&, auto const&) {
        if (m_errorInfo && m_errorInfo.IsOpen())
        {
            m_errorInfo.IsOpen(false);
            ResizeToContent();
        }
    });
    inputSection.Children().Append(m_addressField);
    TextBlock hint;
    hint.Text(L"端口可省略，默认使用 5555。首次连接请在显示器上允许 ADB 授权。");
    hint.FontSize(12);
    hint.TextWrapping(TextWrapping::Wrap);
    hint.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
    inputSection.Children().Append(hint);
    content.Children().Append(inputSection);

    m_errorInfo = InfoBar();
    m_errorInfo.Severity(InfoBarSeverity::Error);
    m_errorInfo.IsClosable(false);
    m_errorInfo.Message(L"请输入有效的 IPv4 地址，端口范围为 1–65535。");
    m_errorInfo.IsOpen(false);
    content.Children().Append(m_errorInfo);

    auto buttons = StackPanel();
    buttons.Orientation(Orientation::Horizontal);
    buttons.HorizontalAlignment(HorizontalAlignment::Right);
    buttons.Spacing(8);
    Button cancel;
    cancel.Content(box_value(L"取消"));
    cancel.MinWidth(88);
    cancel.Click([this](auto const&, auto const&) { Hide(); });
    buttons.Children().Append(cancel);
    Button save;
    save.Content(box_value(L"保存"));
    save.MinWidth(88);
    save.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
    save.Click([this](auto const&, auto const&) { Save(); });
    buttons.Children().Append(save);
    content.Children().Append(buttons);

    m_root.KeyDown([this](auto const&, KeyRoutedEventArgs const& args) {
        if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
        {
            Hide();
            args.Handled(true);
        }
        else if (args.Key() == winrt::Windows::System::VirtualKey::Enter && m_addressField.FocusState() != FocusState::Unfocused)
        {
            Save();
            args.Handled(true);
        }
    });
    m_window.Content(m_root);
}

void SettingsWindow::ApplyTheme()
{
    m_root.Background(Ui::Brush(Ui::Rgba(0, 0, 0, 0)));
    BOOL dark = m_root.ActualTheme() == ElementTheme::Dark;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void SettingsWindow::Save()
{
    auto normalized = Config::Normalize(std::wstring(m_addressField.Text()));
    if (!normalized)
    {
        m_errorInfo.Message(L"请输入有效的 IPv4 地址，端口范围为 1–65535。");
        m_errorInfo.IsOpen(true);
        ResizeToContent();
        m_addressField.Focus(FocusState::Programmatic);
        return;
    }
    if (!Config::SetDeviceAddress(*normalized))
    {
        m_errorInfo.Message(L"无法保存设置，请稍后重试。");
        m_errorInfo.IsOpen(true);
        ResizeToContent();
        return;
    }
    Hide();
    if (OnSaved) OnSaved();
}

void SettingsWindow::Show()
{
    // 重复唤出时保留正在编辑的内容。
    if (!m_appWindow.IsVisible())
    {
        m_addressField.Text(Config::DeviceAddress().value_or(L""));
        m_errorInfo.IsOpen(false);
        ResizeToContent();
        CenterOnScreen();
    }
    m_appWindow.Show();
    m_window.Activate();
    SetForegroundWindow(m_hwnd);
    ResizeToContent();
    m_addressField.Focus(FocusState::Programmatic);
    m_addressField.SelectAll();
}

void SettingsWindow::Hide()
{
    m_appWindow.Hide();
}

void SettingsWindow::ResizeToContent()
{
    // 设置客户区大小，避免标题栏或不可见边框挤占内容。
    constexpr float widthDip = 460;
    m_root.Measure({ widthDip, std::numeric_limits<float>::infinity() });
    double scale = static_cast<double>(GetDpiForWindow(m_hwnd)) / 96.0;
    auto height = std::max(300.0f, m_root.DesiredSize().Height);
    m_appWindow.ResizeClient({ static_cast<int>(std::ceil(widthDip * scale)), static_cast<int>(std::ceil(height * scale)) });
}

void SettingsWindow::CenterOnScreen()
{
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO info{ sizeof(info) };
    if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info)) return;
    auto size = m_appWindow.Size();
    int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - size.Width) / 2;
    int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - size.Height) / 2;
    m_appWindow.Move({ x, y });
}
