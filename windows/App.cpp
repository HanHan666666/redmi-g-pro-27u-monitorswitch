#include "pch.h"

#include "App.h"

#include "Config.h"
#include "Log.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Dispatching;
using namespace winrt::Microsoft::UI::Xaml;

App::App()
{
    Log::Line(L"App::App");



    // 单实例：第二次启动只负责把已有实例的面板叫出来
    m_instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\MonitorSwitch.Tray.SingleInstance");
    if (m_instanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND existing = FindWindowW(kTrayWindowClass, nullptr))
        {
            PostMessageW(existing, kTrayShowPanelMessage, 0, 0);
        }
        m_secondaryInstance = true;
    }

    UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& args) {
        // 这段代码运行在 XAML 的异常处理路径上，任何一次失败的 ABI 调用都会
        // 直接带走整个进程，所以整体包一层 try/catch，只记录最必要的信息。
        try
        {
            wchar_t code[16]{};
            swprintf_s(code, L"%08X", static_cast<uint32_t>(static_cast<int32_t>(args.Exception())));
            Log::Line(std::wstring(L"UnhandledException hr=0x") + code);

            BSTR description = nullptr;
            BSTR restricted = nullptr;
            BSTR capability = nullptr;
            IRestrictedErrorInfo* raw = nullptr;
            HRESULT inner = S_OK;
            if (SUCCEEDED(GetRestrictedErrorInfo(&raw)) && raw != nullptr &&
                SUCCEEDED(raw->GetErrorDetails(&description, &inner, &restricted, &capability)) &&
                description != nullptr)
            {
                Log::Line(std::wstring(L"UnhandledException detail=") + description);
            }
            SysFreeString(description);
            SysFreeString(restricted);
            SysFreeString(capability);
            if (raw != nullptr)
            {
                raw->Release();
            }

            // 托盘常驻程序宁可继续运行，也不要因为一次界面异常整体退出。
            args.Handled(true);
        }
        catch (...)
        {
            Log::Line(L"UnhandledException handler threw");
        }
    });
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType App::GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type)
{
    return m_metadataProvider.GetXamlType(type);
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType App::GetXamlType(winrt::hstring const& name)
{
    return m_metadataProvider.GetXamlType(name);
}

winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> App::GetXmlnsDefinitions()
{
    return m_metadataProvider.GetXmlnsDefinitions();
}

void App::OnLaunched(LaunchActivatedEventArgs const&)
{
    Log::Line(L"OnLaunched start");
    try
    {
        if (m_secondaryInstance)
        {
            Application::Current().Exit();
            return;
        }

        m_state = std::make_shared<AppState>();
        Log::Line(L"state created");

        m_tray = std::make_unique<TrayIcon>();
        m_tray->Install(
            [this] {
                EnsurePanel();
                m_panel->TogglePanel();
            },
            [this](int command) { OnTrayCommand(command); });
        Log::Line(L"tray installed");

        m_state->AddChangeHandler([this] {
            if (m_panel && m_panel->IsVisible())
            {
                m_panel->ApplyState();
            }
            UpdateTrayIcon();
        });
        UpdateTrayIcon();

        auto dispatcher = DispatcherQueue::GetForCurrentThread();

        m_pollTimer = dispatcher.CreateTimer();
        m_pollTimer.Interval(std::chrono::seconds(10));
        m_pollTimer.IsRepeating(true);
        m_pollTimer.Tick([this](auto const&, auto const&) {
            if (m_state)
            {
                m_state->Refresh();
            }
        });
        m_pollTimer.Start();

        m_state->Refresh();
        Log::Line(L"polling started");

        // 首次运行：弹设置面板
        if (!Config::DeviceAddress())
        {
            m_firstRunTimer = dispatcher.CreateTimer();
            m_firstRunTimer.Interval(std::chrono::milliseconds(800));
            m_firstRunTimer.IsRepeating(false);
            m_firstRunTimer.Tick([this](auto const&, auto const&) {
                m_firstRunTimer.Stop();
                OpenSettings();
            });
            m_firstRunTimer.Start();
            Log::Line(L"first run timer armed");
        }

        Log::Line(L"OnLaunched done");
        wchar_t debug[8]{};
        if (GetEnvironmentVariableW(L"MONITORSWITCH_NO_AUTO_HIDE", debug, 8) > 0 && debug[0] == L'1')
        {
            EnsurePanel();
        }
    }
    catch (winrt::hresult_error const& error)
    {
        Log::Line(L"OnLaunched hresult_error 0x" + std::to_wstring(static_cast<uint32_t>(error.code())) + L" " + std::wstring(error.message()));
        throw;
    }
    catch (std::exception const&)
    {
        Log::Line(L"OnLaunched std::exception");
        throw;
    }
    catch (...)
    {
        Log::Line(L"OnLaunched unknown exception");
        throw;
    }
}

void App::EnsureUiResources()
{
    if (m_uiResourcesReady) return;
    // 仅在首次打开窗口时加载 Fluent 控件样式，纯托盘运行不需要控件资源。
    Resources().MergedDictionaries().Append(winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources());
    m_uiResourcesReady = true;
}

void App::EnsurePanel()
{
    if (m_panel) return;
    EnsureUiResources();
    m_panel = std::make_unique<MainWindow>(m_state);
    m_panel->SetTrayRectProvider([this](RECT& rect) { return m_tray != nullptr && m_tray->GetIconRect(rect); });
    m_panel->OnOpenSettings = [this] { OpenSettings(); };
    m_panel->OnExit = [this] { ExitApp(); };
    Log::Line(L"panel created");
}

void App::OnTrayCommand(int command)
{
    switch (command)
    {
    case 1:
        EnsurePanel();
        m_panel->ShowPanel();
        break;
    case 2:
        if (m_state)
        {
            m_state->Refresh();
        }
        break;
    case 3:
        OpenSettings();
        break;
    case 4:
        ExitApp();
        break;
    default:
        break;
    }
}

void App::OpenSettings()
{
    Log::Line(L"OpenSettings");
    try
    {
        EnsureUiResources();
        if (!m_settings)
        {
            m_settings = std::make_unique<SettingsWindow>();
            m_settings->OnSaved = [this] {
                if (m_state)
                {
                    m_state->Refresh();
                }
            };
            Log::Line(L"settings window created");
        }
        m_settings->Show();
        Log::Line(L"settings window shown");
    }
    catch (winrt::hresult_error const& error)
    {
        Log::Line(L"OpenSettings hresult_error 0x" + std::to_wstring(static_cast<uint32_t>(error.code())) + L" " + std::wstring(error.message()));
    }
    catch (...)
    {
        Log::Line(L"OpenSettings unknown exception");
    }
}

void App::ExitApp()
{
    if (m_pollTimer)
    {
        m_pollTimer.Stop();
    }
    if (m_firstRunTimer)
    {
        m_firstRunTimer.Stop();
    }
    if (m_tray)
    {
        m_tray->Remove();
    }
    Application::Current().Exit();
}

void App::UpdateTrayIcon()
{
    if (!m_tray || !m_state)
    {
        return;
    }

    auto address = Config::DeviceAddress().value_or(L"");
    TrayVisualState state = TrayVisualState::NotConfigured;
    std::wstring tooltip = L"MonitorSwitch · 未配置设备地址";

    if (m_state->AdbMissing())
    {
        state = TrayVisualState::AdbMissing;
        tooltip = L"MonitorSwitch · 未找到 adb，请安装 platform-tools";
    }
    else if (!m_state->Configured())
    {
        state = TrayVisualState::NotConfigured;
        tooltip = L"MonitorSwitch · 未配置设备地址";
    }
    else if (m_state->Online())
    {
        state = TrayVisualState::Online;
        tooltip = std::wstring(L"MonitorSwitch · 已连接 ") + address + L" · 当前: " + m_state->CurrentSourceName();
    }
    else
    {
        state = TrayVisualState::Offline;
        tooltip = std::wstring(L"MonitorSwitch · 未连接 ") + address;
    }

    m_tray->SetVisualState(state, tooltip);
}
