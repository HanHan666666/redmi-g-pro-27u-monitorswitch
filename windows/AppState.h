#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>

// 应用状态 + 设备操作。约定：所有成员读写都发生在 UI 线程；
// 后台线程只使用传入的快照，结果通过 DispatcherQueue 回到 UI 线程再落状态。
class AppState : public std::enable_shared_from_this<AppState>
{
public:
    AppState();
    ~AppState();

    AppState(AppState const&) = delete;
    AppState& operator=(AppState const&) = delete;

    void AddChangeHandler(std::function<void()> handler);

    bool Online() const { return m_online; }
    bool AdbMissing() const { return m_adbMissing; }
    bool Configured() const { return m_configured; }
    std::optional<int> CurrentSource() const { return m_currentSource; }
    double Volume() const { return m_volume; }
    double VolumeMax() const { return m_volumeMax; }
    double Backlight() const { return m_backlight; }
    std::wstring CurrentSourceName() const;

    void Refresh();
    void SwitchSource(int sourceId);

    void BeginVolumeDrag();
    void SetVolume(int target);

    void BeginBacklightDrag();
    void SetBacklight(int target);

    void ToggleMute();

private:
    void Post(std::function<void()> callback);
    void PollOnce(bool needDeploy);
    void ApplyVolume(int target);
    void Notify();

    winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
    std::vector<std::function<void()>> m_handlers;

    bool m_online{ false };
    bool m_adbMissing{ false };
    bool m_configured{ false };
    std::optional<int> m_currentSource;
    double m_volume{ 0 };
    double m_volumeMax{ 100 };
    double m_backlight{ 0 };

    int m_lastKnownVolume{ -1 }; // -1 = 尚未从设备读到
    int m_lastKnownBacklight{ -1 };
    bool m_draggingVolume{ false };
    bool m_draggingBacklight{ false };
    bool m_jarReady{ false };

    std::chrono::steady_clock::time_point m_volumeHoldUntil{};
    std::chrono::steady_clock::time_point m_backlightHoldUntil{};

    std::atomic<bool> m_polling{ false };
};
