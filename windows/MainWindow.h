#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include "AppState.h"
#include "UiTheme.h"
#include "PanelPlacement.h"

// 托盘弹出面板：无边框 WinUI 3 窗口，内容全部用 C++ 构建。
class MainWindow
{
public:
    explicit MainWindow(std::shared_ptr<AppState> state);
    ~MainWindow();

    MainWindow(MainWindow const&) = delete;
    MainWindow& operator=(MainWindow const&) = delete;

    void ApplyState();
    void TogglePanel();
    void ShowPanel();
    void HidePanel();
    bool IsVisible() const;

    void SetTrayRectProvider(std::function<bool(RECT&)> provider) { m_trayRectProvider = std::move(provider); }

    std::function<void()> OnOpenSettings;
    std::function<void()> OnExit;

private:
    struct SourceCard
    {
        int id{ 0 };
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton button{ nullptr };
    };

    void SetupWindow();
    void BuildUi();
    void BuildSourceCards();
    winrt::Microsoft::UI::Xaml::Controls::Grid BuildBacklightRow();
    winrt::Microsoft::UI::Xaml::Controls::Grid BuildVolumeRow();
    void WireSliders();
    void CreateCommitTimers();
    void ApplyPalette();
    void UpdateStatusRow();
    void UpdateSourceCards();
    void UpdateVolumeGlyph();
    void SyncSliders();
    void ResizeToFitContent();
    void CapturePanelAnchor();
    void PositionPanel();
    static LRESULT CALLBACK FrameSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR refData);
    void ApplyRoundedRegion(int width, int height, double scale);
    void OpenSettings();
    void StartCommit(bool volume);
    void StopCommit(bool volume);
    void CommitVolume();
    void CommitBacklight();
    double ScaleFactor() const;

    std::shared_ptr<AppState> m_state;
    winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
    HWND m_hwnd{ nullptr };
    bool m_dwmCorners{ false };
    std::optional<PanelPlacement::Anchor> m_panelAnchor;
    bool m_resizing{ false };
    bool m_dark{ false };
    UiPalette m_palette{};

    winrt::Microsoft::UI::Xaml::Controls::Grid m_root{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Border m_panelRoot{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Border m_footerSurface{ nullptr };
    winrt::Microsoft::UI::Xaml::Shapes::Ellipse m_statusDot{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_statusText{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button m_configureButton{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Grid m_sourceGrid{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Slider m_backlightSlider{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_backlightValue{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::FontIcon m_backlightIcon{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Slider m_volumeSlider{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_volumeValue{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::FontIcon m_volumeIcon{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button m_muteButton{ nullptr };

    std::vector<SourceCard> m_cards;
    std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> m_dividers;
    std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_secondaryTexts;
    std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_primaryTexts;
    std::vector<winrt::Microsoft::UI::Xaml::Controls::FontIcon> m_secondaryIcons;

    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_volumeCommitTimer{ nullptr };
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_backlightCommitTimer{ nullptr };

    std::function<bool(RECT&)> m_trayRectProvider;
    bool m_volumeDragging{ false };
    bool m_backlightDragging{ false };
    bool m_syncingVolume{ false };
    bool m_syncingBacklight{ false };
    double m_panelWidthDip{ 360.0 };
    std::chrono::steady_clock::time_point m_lastHidden{};
};
