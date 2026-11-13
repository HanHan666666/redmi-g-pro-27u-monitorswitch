#include "pch.h"

#include "App.h"
#include "Crash.h"
#include "Log.h"

// 入口：初始化单线程 apartment，交给 WinUI 3 启动应用对象。
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    Crash::Install();
    Log::Line(L"main thread " + std::to_wstring(GetCurrentThreadId()));

    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) { winrt::make<App>(); });
    return 0;
}
