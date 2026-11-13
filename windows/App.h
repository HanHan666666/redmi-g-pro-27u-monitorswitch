#pragma once

#include <memory>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

#include "AppState.h"
#include "MainWindow.h"
#include "SettingsWindow.h"
#include "TrayIcon.h"

// 应用对象：托盘图标 + 面板 + 设置窗口 + 10s 轮询。
class App : public winrt::Microsoft::UI::Xaml::ApplicationT<App, winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider>
{
public:
    App();

    winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type);
    winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::hstring const& name);
    winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions();

    void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider m_metadataProvider;
    void EnsureUiResources();
    void EnsurePanel();
    void OpenSettings();
    void ExitApp();
    void UpdateTrayIcon();
    void OnTrayCommand(int command);

    std::shared_ptr<AppState> m_state;
    std::unique_ptr<TrayIcon> m_tray;
    std::unique_ptr<MainWindow> m_panel;
    std::unique_ptr<SettingsWindow> m_settings;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_pollTimer{ nullptr };
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_firstRunTimer{ nullptr };
    HANDLE m_instanceMutex{ nullptr };
    bool m_secondaryInstance{ false };
    bool m_uiResourcesReady{ false };
};
