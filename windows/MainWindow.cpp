#include "pch.h"

#include "MainWindow.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <microsoft.ui.xaml.window.h>
#include <commctrl.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>

#include "Config.h"
#include "Device.h"
#include "Log.h"

using namespace winrt;
using namespace winrt::Microsoft::UI;
using namespace winrt::Microsoft::UI::Dispatching;
using namespace winrt::Microsoft::UI::Windowing;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Media;

// 不能 using namespace Shapes：wingdi.h 里有全局的 Rectangle / Ellipse 函数，会撞名
using XamlEllipse = winrt::Microsoft::UI::Xaml::Shapes::Ellipse;
using XamlRectangle = winrt::Microsoft::UI::Xaml::Shapes::Rectangle;

namespace
{
    // 截图/调试用：设置 MONITORSWITCH_NO_AUTO_HIDE=1 时，面板失活不自动收起
    bool DebugNoAutoHide()
    {
        wchar_t value[8]{};
        DWORD length = GetEnvironmentVariableW(L"MONITORSWITCH_NO_AUTO_HIDE", value, static_cast<DWORD>(std::size(value)));
        return length > 0 && value[0] == L'1';
    }

    Thumb FindThumbInTree(DependencyObject const& node)
    {
        int count = VisualTreeHelper::GetChildrenCount(node);
        for (int index = 0; index < count; ++index)
        {
            auto child = VisualTreeHelper::GetChild(node, index);
            if (auto thumb = child.try_as<Thumb>())
            {
                return thumb;
            }
            if (auto nested = FindThumbInTree(child))
            {
                return nested;
            }
        }
        return nullptr;
    }

    std::wstring FormatValue(double value)
    {
        return std::to_wstring(static_cast<int>(std::lround(value)));
    }

    Button CreateSmallButton(hstring const& text)
    {
        Button button;
        button.Content(box_value(text));
        button.FontSize(12);
        button.Padding(Thickness{ 12, 5, 12, 5 });
        button.MinHeight(32);
        return button;
    }

    void AddColumn(Grid const& grid, GridUnitType type, double value)
    {
        ColumnDefinition definition;
        definition.Width(GridLength{ value, type });
        grid.ColumnDefinitions().Append(definition);
    }

    void AddRow(Grid const& grid, GridUnitType type, double value)
    {
        RowDefinition definition;
        definition.Height(GridLength{ value, type });
        grid.RowDefinitions().Append(definition);
    }

    XamlRectangle CreateDivider()
    {
        XamlRectangle rectangle;
        rectangle.Height(1);
        return rectangle;
    }
}

MainWindow::MainWindow(std::shared_ptr<AppState> state) : m_state(std::move(state))
{
    Log::Line(L"MainWindow: begin");
    m_dark = !Ui::SystemUsesLightTheme();
    m_palette = Ui::Palette(m_dark);

    m_window = Window();
    m_window.Title(L"MonitorSwitch");
    m_appWindow = m_window.AppWindow();
    check_hresult(m_window.as<IWindowNative>()->get_WindowHandle(&m_hwnd));
    Log::Line(L"MainWindow: window created");

    SetupWindow();
    Log::Line(L"MainWindow: chrome ready");
    BuildUi();
    Log::Line(L"MainWindow: ui built");
    CreateCommitTimers();
    WireSliders();
    ApplyPalette();
    ApplyState();
    ResizeToFitContent();
    Log::Line(L"MainWindow: ready");
    if (DebugNoAutoHide()) ShowPanel();
}

MainWindow::~MainWindow()
{
    if (m_hwnd) RemoveWindowSubclass(m_hwnd, &MainWindow::FrameSubclassProc, 1);
}

LRESULT CALLBACK MainWindow::FrameSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR)
{
    // 隐藏标题栏后，WinUI 的普通窗口仍可能保留非客户区边缘。
    // 整个窗口交给 XAML 绘制，DWM 仅负责外侧阴影和圆角。
    if (message == WM_NCCALCSIZE) return 0;
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, &MainWindow::FrameSubclassProc, subclassId);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void MainWindow::SetupWindow()
{
    if (!m_appWindow || m_hwnd == nullptr)
    {
        return;
    }

    if (auto presenter = m_appWindow.Presenter().try_as<OverlappedPresenter>())
    {
        presenter.SetBorderAndTitleBar(false, false);
        presenter.IsResizable(false);
        presenter.IsMaximizable(false);
        presenter.IsMinimizable(false);
        presenter.IsAlwaysOnTop(true);
    }

    LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    // 调试显示模式保留普通窗口入口，便于桌面自动化捕获托盘面板。
    if (DebugNoAutoHide())
    {
        exStyle = (exStyle | WS_EX_APPWINDOW) & ~WS_EX_TOOLWINDOW;
    }
    else
    {
        exStyle = (exStyle | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
    }
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);
    check_bool(SetWindowSubclass(m_hwnd, &MainWindow::FrameSubclassProc, 1, 0));
    check_bool(SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));

    // Win11 用 DWM 圆角；Win10 上 SetWindowRgn 兜底（见 ApplyRoundedRegion）
    DWORD corner = DWMWCP_ROUND;
    m_dwmCorners = SUCCEEDED(DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)));
    COLORREF borderColor = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    m_appWindow.Closing([this](auto const&, AppWindowClosingEventArgs const& args) {
        args.Cancel(true);
        HidePanel();
    });
    m_window.Activated([this](auto const&, WindowActivatedEventArgs const& args) {
        if (args.WindowActivationState() == WindowActivationState::Deactivated && !DebugNoAutoHide())
        {
            HidePanel();
        }
    });
    m_window.SystemBackdrop(DesktopAcrylicBackdrop());
}

void MainWindow::BuildUi()
{
    auto root = Grid();
    m_root = root;

    m_panelRoot = Border();
    // 不在 DWM 外框里再套一层描边和圆角，避免边缘出现双线与方角残影。

    auto surface = StackPanel();
    m_panelRoot.Child(surface);

    auto stack = StackPanel();
    stack.Spacing(16);
    stack.Padding(Thickness{ 24, 20, 24, 20 });
    surface.Children().Append(stack);
    root.Children().Append(m_panelRoot);
    m_window.Content(root);

    // 顶部状态行
    auto header = Grid();
    header.ColumnSpacing(8);
    AddColumn(header, GridUnitType::Auto, 0);
    AddColumn(header, GridUnitType::Star, 1);
    AddColumn(header, GridUnitType::Auto, 0);

    m_statusDot = XamlEllipse();
    m_statusDot.Width(9);
    m_statusDot.Height(9);
    m_statusDot.VerticalAlignment(VerticalAlignment::Center);
    header.Children().Append(m_statusDot);

    m_statusText = TextBlock();
    m_statusText.FontSize(12);
    m_statusText.TextWrapping(TextWrapping::Wrap);
    m_statusText.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(m_statusText, 1);
    header.Children().Append(m_statusText);

    m_configureButton = CreateSmallButton(L"去设置");
    m_configureButton.Visibility(Visibility::Collapsed);
    m_configureButton.Click([this](auto const&, auto const&) { OpenSettings(); });
    Grid::SetColumn(m_configureButton, 2);
    header.Children().Append(m_configureButton);
    stack.Children().Append(header);

    auto headerDivider = CreateDivider();
    m_dividers.push_back(headerDivider);
    stack.Children().Append(headerDivider);

    auto sectionLabel = [this](hstring const& text) {
        TextBlock label;
        label.Text(text);
        label.FontSize(12);
        m_secondaryTexts.push_back(label);
        return label;
    };

    stack.Children().Append(sectionLabel(L"信号源"));

    m_sourceGrid = Grid();
    m_sourceGrid.ColumnSpacing(12);
    m_sourceGrid.RowSpacing(16);
    AddColumn(m_sourceGrid, GridUnitType::Star, 1);
    AddColumn(m_sourceGrid, GridUnitType::Star, 1);
    AddRow(m_sourceGrid, GridUnitType::Auto, 0);
    AddRow(m_sourceGrid, GridUnitType::Auto, 0);
    stack.Children().Append(m_sourceGrid);
    BuildSourceCards();

    stack.Children().Append(sectionLabel(L"背光"));
    stack.Children().Append(BuildBacklightRow());

    stack.Children().Append(sectionLabel(L"音量"));
    stack.Children().Append(BuildVolumeRow());

    auto footerDivider = CreateDivider();
    m_dividers.push_back(footerDivider);
    surface.Children().Append(footerDivider);

    auto footer = Grid();
    footer.ColumnSpacing(8);
    AddColumn(footer, GridUnitType::Auto, 0);
    AddColumn(footer, GridUnitType::Auto, 0);
    AddColumn(footer, GridUnitType::Star, 1);
    AddColumn(footer, GridUnitType::Auto, 0);

    auto settingsButton = CreateSmallButton(L"ADB 设置…");
    settingsButton.Click([this](auto const&, auto const&) { OpenSettings(); });
    footer.Children().Append(settingsButton);

    auto refreshButton = CreateSmallButton(L"刷新");
    refreshButton.Click([this](auto const&, auto const&) {
        if (m_state)
        {
            m_state->Refresh();
        }
    });
    Grid::SetColumn(refreshButton, 1);
    footer.Children().Append(refreshButton);

    auto exitButton = CreateSmallButton(L"退出");
    exitButton.Click([this](auto const&, auto const&) {
        if (OnExit)
        {
            OnExit();
        }
    });
    Grid::SetColumn(exitButton, 3);
    footer.Children().Append(exitButton);

    m_footerSurface = Border();
    m_footerSurface.Padding(Thickness{ 24, 16, 24, 16 });
    m_footerSurface.Child(footer);
    surface.Children().Append(m_footerSurface);

    m_root.Loaded([this](auto const&, auto const&) {
        if (auto xamlRoot = m_root.XamlRoot())
        {
            xamlRoot.Changed([this](auto const&, auto const&) {
                ResizeToFitContent();
                if (m_appWindow && m_appWindow.IsVisible())
                {
                    PositionPanel();
                }
            });
        }
    });

    m_root.ActualThemeChanged([this](auto const&, auto const&) {
        m_dark = !Ui::SystemUsesLightTheme();
        m_palette = Ui::Palette(m_dark);
        ApplyPalette();
    });
}

void MainWindow::BuildSourceCards()
{
    int index = 0;
    for (auto const& item : kSourceItems)
    {
        auto tile = StackPanel();
        tile.Spacing(8);
        ToggleButton button;
        button.Height(56);
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        Automation::AutomationProperties::SetName(button, item.name);
        ToolTipService::SetToolTip(button, box_value(std::wstring(L"切换到 ") + item.name));
        FontIcon icon;
        icon.Glyph(item.id == 30 ? L"\uE88E" : L"\uE7F4");
        icon.FontSize(22);
        button.Content(icon);
        int sourceId = item.id;
        button.Click([this, sourceId](auto const&, auto const&) {
            if (m_state) m_state->SwitchSource(sourceId);
            UpdateSourceCards();
        });
        tile.Children().Append(button);
        TextBlock label;
        label.Text(item.name);
        label.FontSize(12);
        label.HorizontalAlignment(HorizontalAlignment::Center);
        m_primaryTexts.push_back(label);
        tile.Children().Append(label);
        Grid::SetRow(tile, index / 2);
        Grid::SetColumn(tile, index % 2);
        m_sourceGrid.Children().Append(tile);
        m_cards.push_back(SourceCard{ item.id, button });
        ++index;
    }
}

Grid MainWindow::BuildBacklightRow()
{
    auto row = Grid();
    row.ColumnSpacing(10);
    AddColumn(row, GridUnitType::Auto, 0);
    AddColumn(row, GridUnitType::Star, 1);
    AddColumn(row, GridUnitType::Auto, 0);

    m_backlightIcon = FontIcon();
    m_backlightIcon.Glyph(L"\uE706");
    m_backlightIcon.FontSize(14);
    m_backlightIcon.Width(32);
    m_backlightIcon.VerticalAlignment(VerticalAlignment::Center);
    m_secondaryIcons.push_back(m_backlightIcon);
    row.Children().Append(m_backlightIcon);

    m_backlightSlider = Slider();
    m_backlightSlider.Minimum(1);
    m_backlightSlider.Maximum(100);
    m_backlightSlider.StepFrequency(1);
    m_backlightSlider.IsThumbToolTipEnabled(false);
    m_backlightSlider.MinHeight(32);
    Automation::AutomationProperties::SetName(m_backlightSlider, L"背光");
    m_backlightSlider.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(m_backlightSlider, 1);
    row.Children().Append(m_backlightSlider);

    m_backlightValue = TextBlock();
    m_backlightValue.FontSize(12);
    m_backlightValue.TextAlignment(TextAlignment::Right);
    m_backlightValue.Width(28);
    m_backlightValue.VerticalAlignment(VerticalAlignment::Center);
    m_backlightValue.Text(L"0");
    m_secondaryTexts.push_back(m_backlightValue);
    Grid::SetColumn(m_backlightValue, 2);
    row.Children().Append(m_backlightValue);

    return row;
}

Grid MainWindow::BuildVolumeRow()
{
    auto row = Grid();
    row.ColumnSpacing(10);
    AddColumn(row, GridUnitType::Auto, 0);
    AddColumn(row, GridUnitType::Star, 1);
    AddColumn(row, GridUnitType::Auto, 0);

    m_volumeIcon = FontIcon();
    m_volumeIcon.Glyph(L"\uE767");
    m_volumeIcon.FontSize(14);
    m_volumeIcon.VerticalAlignment(VerticalAlignment::Center);

    m_volumeSlider = Slider();
    m_volumeSlider.Minimum(0);
    m_volumeSlider.Maximum(100);
    m_volumeSlider.StepFrequency(1);
    m_volumeSlider.IsThumbToolTipEnabled(false);
    m_volumeSlider.MinHeight(32);
    Automation::AutomationProperties::SetName(m_volumeSlider, L"音量");
    m_volumeSlider.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(m_volumeSlider, 1);
    row.Children().Append(m_volumeSlider);

    m_volumeValue = TextBlock();
    m_volumeValue.FontSize(12);
    m_volumeValue.TextAlignment(TextAlignment::Right);
    m_volumeValue.Width(28);
    m_volumeValue.VerticalAlignment(VerticalAlignment::Center);
    m_volumeValue.Text(L"0");
    m_secondaryTexts.push_back(m_volumeValue);
    Grid::SetColumn(m_volumeValue, 2);
    row.Children().Append(m_volumeValue);

    m_muteButton = Button();
    m_muteButton.Background(Ui::Brush(Ui::Rgba(0, 0, 0, 0)));
    m_muteButton.BorderThickness(Thickness{ 0, 0, 0, 0 });
    m_muteButton.Padding(Thickness{ 4, 4, 4, 4 });
    m_muteButton.MinWidth(28);
    m_muteButton.MinHeight(28);
    m_muteButton.CornerRadius(CornerRadius{ 5, 5, 5, 5 });
    m_muteButton.VerticalAlignment(VerticalAlignment::Center);
    m_muteButton.Width(32);
    m_muteButton.Content(m_volumeIcon);
    ToolTipService::SetToolTip(m_muteButton, box_value(L"静音"));
    Automation::AutomationProperties::SetName(m_muteButton, L"静音或取消静音");
    m_muteButton.Click([this](auto const&, auto const&) {
        if (m_state)
        {
            m_state->ToggleMute();
        }
    });
    row.Children().Append(m_muteButton);

    return row;
}

void MainWindow::CreateCommitTimers()
{
    auto dispatcher = DispatcherQueue::GetForCurrentThread();

    m_volumeCommitTimer = dispatcher.CreateTimer();
    m_volumeCommitTimer.Interval(std::chrono::milliseconds(250));
    m_volumeCommitTimer.IsRepeating(false);
    m_volumeCommitTimer.Tick([this](auto const&, auto const&) {
        m_volumeCommitTimer.Stop();
        CommitVolume();
    });

    m_backlightCommitTimer = dispatcher.CreateTimer();
    m_backlightCommitTimer.Interval(std::chrono::milliseconds(250));
    m_backlightCommitTimer.IsRepeating(false);
    m_backlightCommitTimer.Tick([this](auto const&, auto const&) {
        m_backlightCommitTimer.Stop();
        CommitBacklight();
    });
}

void MainWindow::WireSliders()
{
    if (m_backlightSlider)
    {
        m_backlightSlider.ValueChanged([this](auto const&, RangeBaseValueChangedEventArgs const& args) {
            if (m_syncingBacklight)
            {
                return;
            }
            if (m_backlightValue)
            {
                m_backlightValue.Text(hstring(FormatValue(args.NewValue())));
            }
            if (!m_backlightDragging)
            {
                StartCommit(false);
            }
        });

        m_backlightSlider.Loaded([this](auto const&, auto const&) {
            if (auto thumb = FindThumbInTree(m_backlightSlider))
            {
                thumb.DragStarted([this](auto const&, auto const&) {
                    m_backlightDragging = true;
                    StopCommit(false);
                    if (m_state)
                    {
                        m_state->BeginBacklightDrag();
                    }
                });
                thumb.DragCompleted([this](auto const&, auto const&) {
                    m_backlightDragging = false;
                    CommitBacklight();
                });
            }
        });
    }

    if (m_volumeSlider)
    {
        m_volumeSlider.ValueChanged([this](auto const&, RangeBaseValueChangedEventArgs const& args) {
            if (m_syncingVolume)
            {
                return;
            }
            if (m_volumeValue)
            {
                m_volumeValue.Text(hstring(FormatValue(args.NewValue())));
            }
            if (!m_volumeDragging)
            {
                StartCommit(true);
            }
        });

        m_volumeSlider.Loaded([this](auto const&, auto const&) {
            if (auto thumb = FindThumbInTree(m_volumeSlider))
            {
                thumb.DragStarted([this](auto const&, auto const&) {
                    m_volumeDragging = true;
                    StopCommit(true);
                    if (m_state)
                    {
                        m_state->BeginVolumeDrag();
                    }
                });
                thumb.DragCompleted([this](auto const&, auto const&) {
                    m_volumeDragging = false;
                    CommitVolume();
                });
            }
        });
    }
}

void MainWindow::ApplyPalette()
{
    if (!m_root)
    {
        return;
    }

    // 系统亚克力已经包含模糊、噪点与明暗遮罩；内容层必须透出这层材质。
    // 在整窗上重复叠加 LayerFillColorDefaultBrush 会把磨砂背景刷成白板。
    m_root.Background(Ui::Brush(Ui::Rgba(0, 0, 0, 0)));
    m_panelRoot.Background(nullptr);
    m_footerSurface.Background(Application::Current().Resources().Lookup(box_value(L"SubtleFillColorSecondaryBrush")).as<Brush>());

    for (auto const& divider : m_dividers)
    {
        divider.Fill(Ui::Brush(m_palette.Divider));
    }
    for (auto const& text : m_secondaryTexts)
    {
        text.Foreground(Ui::Brush(m_palette.SecondaryText));
    }
    for (auto const& text : m_primaryTexts)
    {
        text.Foreground(Ui::Brush(m_palette.PrimaryText));
    }
    for (auto const& icon : m_secondaryIcons)
    {
        icon.Foreground(Ui::Brush(m_palette.SecondaryText));
    }

    UpdateStatusRow();
}

void MainWindow::OpenSettings()
{
    HidePanel();
    if (OnOpenSettings)
    {
        OnOpenSettings();
    }
}

void MainWindow::ApplyState()
{
    if (!m_state)
    {
        return;
    }

    UpdateStatusRow();
    UpdateSourceCards();

    bool online = m_state->Online();
    if (m_backlightSlider)
    {
        m_backlightSlider.IsEnabled(online && m_state->Backlight() > 0);
    }
    if (m_volumeSlider)
    {
        m_volumeSlider.IsEnabled(online);
    }
    if (m_muteButton)
    {
        m_muteButton.IsEnabled(online);
    }

    SyncSliders();
    UpdateVolumeGlyph();
    ResizeToFitContent();

    if (m_appWindow && m_appWindow.IsVisible())
    {
        PositionPanel();
    }
}

void MainWindow::UpdateStatusRow()
{
    if (!m_statusText || !m_state)
    {
        return;
    }

    auto address = Config::DeviceAddress();
    if (m_state->AdbMissing())
    {
        m_statusText.Text(L"未找到 adb，请安装 platform-tools");
        m_statusText.Foreground(Ui::Brush(m_palette.PrimaryText));
        if (m_statusDot)
        {
            m_statusDot.Fill(Ui::Brush(m_palette.Unknown));
        }
        if (m_configureButton)
        {
            m_configureButton.Visibility(Visibility::Collapsed);
        }
    }
    else if (!m_state->Configured())
    {
        m_statusText.Text(L"未配置设备地址");
        m_statusText.Foreground(Ui::Brush(m_palette.PrimaryText));
        if (m_statusDot)
        {
            m_statusDot.Fill(Ui::Brush(m_palette.Unknown));
        }
        if (m_configureButton)
        {
            m_configureButton.Visibility(Visibility::Visible);
        }
    }
    else
    {
        std::wstring text = m_state->Online()
            ? std::wstring(L"已连接 ") + address.value_or(L"") + L" · 当前: " + m_state->CurrentSourceName()
            : std::wstring(L"未连接 ") + address.value_or(L"");
        m_statusText.Text(hstring(text));
        m_statusText.Foreground(Ui::Brush(m_state->Online() ? m_palette.PrimaryText : m_palette.Offline));
        if (m_statusDot)
        {
            m_statusDot.Fill(Ui::Brush(m_state->Online() ? m_palette.Online : m_palette.Offline));
        }
        if (m_configureButton)
        {
            m_configureButton.Visibility(Visibility::Collapsed);
        }
    }
}

void MainWindow::UpdateSourceCards()
{
    if (!m_state)
    {
        return;
    }
    auto current = m_state->CurrentSource();
    for (auto const& card : m_cards)
    {
        bool selected = current && *current == card.id;
        card.button.IsChecked(selected);
        card.button.IsEnabled(m_state->Online());
    }
}

void MainWindow::UpdateVolumeGlyph()
{
    if (!m_volumeIcon || !m_state)
    {
        return;
    }
    m_volumeIcon.Glyph(m_state->Volume() <= 0 ? L"\uE74F" : L"\uE767");
}

void MainWindow::SyncSliders()
{
    if (!m_state)
    {
        return;
    }

    if (m_backlightSlider)
    {
        if (!m_backlightDragging)
        {
            m_syncingBacklight = true;
            m_backlightSlider.Value(m_state->Backlight());
            m_syncingBacklight = false;
        }
        if (m_backlightValue)
        {
            m_backlightValue.Text(hstring(FormatValue(m_backlightSlider.Value())));
        }
    }

    if (m_volumeSlider)
    {
        if (!m_volumeDragging)
        {
            m_syncingVolume = true;
            double maximum = std::max(1.0, m_state->VolumeMax());
            if (std::abs(m_volumeSlider.Maximum() - maximum) > 0.5)
            {
                m_volumeSlider.Maximum(maximum);
            }
            m_volumeSlider.Value(m_state->Volume());
            m_syncingVolume = false;
        }
        if (m_volumeValue)
        {
            m_volumeValue.Text(hstring(FormatValue(m_volumeSlider.Value())));
        }
    }
}

void MainWindow::StartCommit(bool volume)
{
    if (volume)
    {
        if (m_volumeCommitTimer)
        {
            m_volumeCommitTimer.Stop();
            m_volumeCommitTimer.Start();
        }
    }
    else if (m_backlightCommitTimer)
    {
        m_backlightCommitTimer.Stop();
        m_backlightCommitTimer.Start();
    }
}

void MainWindow::StopCommit(bool volume)
{
    if (volume)
    {
        if (m_volumeCommitTimer)
        {
            m_volumeCommitTimer.Stop();
        }
    }
    else if (m_backlightCommitTimer)
    {
        m_backlightCommitTimer.Stop();
    }
}

void MainWindow::CommitVolume()
{
    if (!m_state || !m_volumeSlider || m_syncingVolume)
    {
        return;
    }
    m_state->SetVolume(static_cast<int>(std::lround(m_volumeSlider.Value())));
}

void MainWindow::CommitBacklight()
{
    if (!m_state || !m_backlightSlider || m_syncingBacklight)
    {
        return;
    }
    m_state->SetBacklight(static_cast<int>(std::lround(m_backlightSlider.Value())));
}

double MainWindow::ScaleFactor() const
{
    UINT dpi = m_hwnd ? GetDpiForWindow(m_hwnd) : 96;
    return dpi == 0 ? 1.0 : static_cast<double>(dpi) / 96.0;
}

void MainWindow::ResizeToFitContent()
{
    if (!m_panelRoot || !m_appWindow || m_resizing)
    {
        return;
    }

    m_resizing = true;
    struct ResetFlag
    {
        bool& flag;
        ~ResetFlag() { flag = false; }
    } reset{ m_resizing };

    m_panelRoot.Measure(winrt::Windows::Foundation::Size(static_cast<float>(m_panelWidthDip), std::numeric_limits<float>::infinity()));
    double height = std::ceil(m_panelRoot.DesiredSize().Height);
    if (height < 80.0)
    {
        height = 80.0;
    }
    double scale = ScaleFactor();
    int width = static_cast<int>(std::lround(m_panelWidthDip * scale));
    int pixels = static_cast<int>(std::lround(height * scale));
    // WM_NCCALCSIZE 已使窗口与客户区一致，直接设置外部尺寸。
    // ResizeClient 会再次补入默认窗口框的厚度，留下额外像素。
    auto size = m_appWindow.Size();
    if (size.Width != width || size.Height != pixels)
    {
        m_appWindow.Resize({ width, pixels });
    }
    ApplyRoundedRegion(width, pixels, scale);
}

void MainWindow::ApplyRoundedRegion(int width, int height, double scale)
{
    if (m_dwmCorners || m_hwnd == nullptr)
    {
        return;
    }
    int diameter = static_cast<int>(std::lround(16.0 * scale));
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, diameter, diameter);
    if (region != nullptr)
    {
        if (!SetWindowRgn(m_hwnd, region, TRUE)) DeleteObject(region);
    }
}

void MainWindow::CapturePanelAnchor()
{
    // 只在打开时用图标选择屏幕；不保存抽屉中图标的临时位置。
    RECT iconRect{};
    bool hasIcon = m_trayRectProvider && m_trayRectProvider(iconRect)
        && iconRect.right > iconRect.left && iconRect.bottom > iconRect.top;
    POINT point{};
    if (hasIcon)
    {
        point = { (iconRect.left + iconRect.right) / 2, (iconRect.top + iconRect.bottom) / 2 };
    }
    else
    {
        GetCursorPos(&point);
    }

    MONITORINFO info{ sizeof(info) };
    if (!GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info))
    {
        m_panelAnchor.reset();
        return;
    }
    auto toRect = [](RECT rect) {
        return PanelPlacement::Rect{ rect.left, rect.top, rect.right, rect.bottom };
    };
    m_panelAnchor = PanelPlacement::FromMonitor(toRect(info.rcMonitor), toRect(info.rcWork));
}

void MainWindow::PositionPanel()
{
    if (!m_appWindow || !m_panelAnchor) return;
    auto size = m_appWindow.Size();
    int margin = static_cast<int>(std::lround(8.0 * ScaleFactor()));
    auto position = PanelPlacement::Place(*m_panelAnchor, size.Width, size.Height, margin);
    auto current = m_appWindow.Position();
    if (current.X != position.x || current.Y != position.y)
    {
        m_appWindow.Move({ position.x, position.y });
    }
}

bool MainWindow::IsVisible() const
{
    return m_appWindow && m_appWindow.IsVisible();
}

void MainWindow::ShowPanel()
{
    if (!m_appWindow || !m_window)
    {
        return;
    }

    if (!m_appWindow.IsVisible()) CapturePanelAnchor();
    ApplyState();
    ResizeToFitContent();
    PositionPanel();
    if (!m_appWindow.IsVisible())
    {
        m_appWindow.Show();
    }
    m_window.Activate();
    SetForegroundWindow(m_hwnd);

    // 跨屏时 DPI 会变，显示后再校正一次
    ResizeToFitContent();
    PositionPanel();
    if (DebugNoAutoHide())
    {
        RECT outer{}, client{};
        POINT origin{};
        GetWindowRect(m_hwnd, &outer);
        GetClientRect(m_hwnd, &client);
        ClientToScreen(m_hwnd, &origin);
        Log::Line(L"Panel geometry: position=" + std::to_wstring(outer.left) + L"," + std::to_wstring(outer.top)
            + L" outer=" + std::to_wstring(outer.right - outer.left) + L"x" + std::to_wstring(outer.bottom - outer.top)
            + L" client=" + std::to_wstring(client.right) + L"x" + std::to_wstring(client.bottom)
            + L" inset=" + std::to_wstring(origin.x - outer.left) + L"," + std::to_wstring(origin.y - outer.top));
    }
}

void MainWindow::HidePanel()
{
    if (!m_appWindow)
    {
        return;
    }
    if (m_appWindow.IsVisible())
    {
        m_appWindow.Hide();
        m_lastHidden = std::chrono::steady_clock::now();
    }
}

void MainWindow::TogglePanel()
{
    if (!m_appWindow)
    {
        return;
    }
    if (m_appWindow.IsVisible())
    {
        HidePanel();
        return;
    }
    // 点击托盘图标会先让面板失去激活（从而被隐藏），这一次点击不应再把它打开
    if (std::chrono::steady_clock::now() - m_lastHidden < std::chrono::milliseconds(250))
    {
        return;
    }
    ShowPanel();
}
