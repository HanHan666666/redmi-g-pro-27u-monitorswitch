#include "pch.h"

#include "AppState.h"
#include "Adb.h"
#include "Config.h"
#include "Device.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr auto kVolumeHoldAfterRelease = std::chrono::milliseconds(1500);
    constexpr auto kVolumeHoldAfterApply = std::chrono::milliseconds(1200);
    constexpr auto kBacklightHoldAfterSet = std::chrono::milliseconds(1500);

    void DebugLog(std::wstring const& text)
    {
        OutputDebugStringW(text.c_str());
        OutputDebugStringW(L"\n");
    }
}

AppState::AppState()
{
    m_dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
}

AppState::~AppState() = default;

void AppState::AddChangeHandler(std::function<void()> handler)
{
    if (handler)
    {
        m_handlers.push_back(std::move(handler));
    }
}

std::wstring AppState::CurrentSourceName() const
{
    if (!m_currentSource)
    {
        return L"未知";
    }
    for (auto const& item : kSourceItems)
    {
        if (item.id == *m_currentSource)
        {
            return item.name;
        }
    }
    return L"未知";
}

void AppState::Post(std::function<void()> callback)
{
    auto dispatcher = m_dispatcher;
    if (!dispatcher)
    {
        callback();
        return;
    }
    dispatcher.TryEnqueue([callback = std::move(callback)]() { callback(); });
}

void AppState::Notify()
{
    for (auto const& handler : m_handlers)
    {
        handler();
    }
}

void AppState::Refresh()
{
    // 轮询不重叠：设备忙时跳过这一次，避免排队堆积
    if (m_polling.exchange(true))
    {
        return;
    }
    bool needDeploy = !m_jarReady;
    auto self = shared_from_this();
    try
    {
        std::thread([self, needDeploy] { self->PollOnce(needDeploy); }).detach();
    }
    catch (...)
    {
        m_polling = false;
    }
}

void AppState::PollOnce(bool needDeploy)
{
    std::optional<std::wstring> address;
    bool adbFound = false;
    bool online = false;
    std::optional<int> source;
    std::optional<std::pair<int, int>> volume;
    std::optional<int> backlight;

    try
    {
        address = Config::DeviceAddress();
        adbFound = Adb::Binary().has_value();
        if (address && adbFound)
        {
            online = Adb::Connect();
            if (online)
            {
                if (auto raw = Adb::Shell(L"settings get global mitv.tvplayer.hdmi.last.source"))
                {
                    source = Util::ParseInt(*raw);
                }
                volume = QueryVolume();
                if (auto raw = Adb::Shell(L"settings get global picture_backlight"))
                {
                    backlight = Util::ParseInt(*raw);
                }
                // 首次连上：部署寄存器/音量直写工具并读一次真实背光校准
                if (needDeploy)
                {
                    Mtk::DeployJar();
                    VolDirect::DeployJar();
                    if (auto real = Mtk::ReadBacklight())
                    {
                        backlight = real;
                    }
                }
            }
        }
    }
    catch (...)
    {
        // adb 层已保证不抛异常，这里只兜底
    }

    auto self = shared_from_this();
    Post([self, address, adbFound, online, source, volume, backlight, needDeploy] {
        auto now = std::chrono::steady_clock::now();
        self->m_configured = address.has_value();
        self->m_adbMissing = !adbFound;
        self->m_online = online;
        self->m_currentSource = source;

        if (backlight && now >= self->m_backlightHoldUntil)
        {
            self->m_backlight = *backlight;
            self->m_lastKnownBacklight = *backlight;
        }
        if (volume)
        {
            self->m_volumeMax = volume->second > 0 ? volume->second : 100;
            // 拖动期间及发送后短时间内不回写轮询值，避免滑块被设备端的旧值拉回
            if (now >= self->m_volumeHoldUntil)
            {
                self->m_volume = volume->first;
                self->m_lastKnownVolume = volume->first;
            }
        }
        if (online && needDeploy)
        {
            self->m_jarReady = true;
        }
        self->m_polling = false;
        self->Notify();
    });
}

void AppState::SwitchSource(int sourceId)
{
    auto self = shared_from_this();
    try
    {
        std::thread([self, sourceId] {
            try
            {
                if (!Adb::Connect())
                {
                    self->Post([self] {
                        self->m_online = false;
                        self->Notify();
                    });
                    return;
                }
                // 与 Toolbox 相同的顺序：先停播放器再拉起目标源，避免停在原 Activity 上
                Adb::Shell(L"am force-stop com.xiaomi.mitv.tvplayer");
                Adb::Shell(std::wstring(L"am start -a com.xiaomi.mitv.tvplayer.EXTSRC_PLAY "
                                        L"-n com.xiaomi.mitv.tvplayer/.ExternalSourceActivity "
                                        L"--ei input ") +
                           std::to_wstring(sourceId) + L" -f 0x10000000");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 等系统把 last.source 写完

                auto raw = Adb::Shell(L"settings get global mitv.tvplayer.hdmi.last.source");
                auto parsed = raw ? Util::ParseInt(*raw) : std::nullopt;
                self->Post([self, parsed, sourceId] {
                    self->m_currentSource = parsed.value_or(sourceId);
                    self->Notify();
                });
            }
            catch (...)
            {
            }
        }).detach();
    }
    catch (...)
    {
    }
}

void AppState::BeginVolumeDrag()
{
    m_draggingVolume = true;
    m_volumeHoldUntil = std::chrono::steady_clock::time_point::max();
}

void AppState::SetVolume(int target)
{
    m_draggingVolume = false;
    m_volumeHoldUntil = std::chrono::steady_clock::now() + kVolumeHoldAfterRelease;
    ApplyVolume(target);
}

void AppState::ApplyVolume(int target)
{
    if (m_lastKnownVolume < 0)
    {
        return;
    }

    int current = m_lastKnownVolume;
    if (target == current)
    {
        return;
    }

    int maxVolume = static_cast<int>(std::lround(m_volumeMax));
    int predicted = std::clamp(target, 0, maxVolume > 0 ? maxVolume : 100);
    m_lastKnownVolume = predicted;
    m_volume = predicted;
    m_volumeHoldUntil = std::chrono::steady_clock::now() + kVolumeHoldAfterApply;
    DebugLog(L"音量 " + std::to_wstring(current) + L" -> " + std::to_wstring(predicted));
    Notify();

    auto self = shared_from_this();
    try
    {
        std::thread([self, current, predicted] {
            try
            {
                // 首选：VolDirectTool 直写（system 身份 + AudioManager，与小爱同路，瞬时）
                VolDirect::Set(predicted);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (auto volume = QueryVolume(); volume && volume->first == predicted)
                {
                    return; // 直写成功
                }

                // 回退：keyevent 步进（固件限制 ~70ms/步）
                int actualValue = current;
                if (auto volume = QueryVolume())
                {
                    actualValue = volume->first;
                }
                int step = predicted - actualValue;
                if (step == 0)
                {
                    return;
                }
                std::wstring keycode = step > 0 ? L"24" : L"25"; // 24=VOLUME_UP, 25=VOLUME_DOWN
                std::wstring command = L"input keyevent";
                int repeats = std::min(std::abs(step), 200);
                for (int i = 0; i < repeats; ++i)
                {
                    command.push_back(L' ');
                    command += keycode;
                }
                Adb::Exec({ L"shell", command }, 15000);
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                if (auto finalVolume = QueryVolume())
                {
                    self->Post([self, value = finalVolume->first] { self->m_lastKnownVolume = value; });
                }
            }
            catch (...)
            {
            }
        }).detach();
    }
    catch (...)
    {
    }
}

void AppState::BeginBacklightDrag()
{
    m_draggingBacklight = true;
    m_backlightHoldUntil = std::chrono::steady_clock::time_point::max();
}

void AppState::SetBacklight(int target)
{
    m_draggingBacklight = false;
    m_backlightHoldUntil = std::chrono::steady_clock::now() + kBacklightHoldAfterSet;
    if (m_lastKnownBacklight < 0 || target == m_lastKnownBacklight)
    {
        return;
    }

    int predicted = std::clamp(target, 1, 100); // 工具箱下限是 1：0 可能黑屏
    m_lastKnownBacklight = predicted;
    m_backlight = predicted;
    Notify();

    auto self = shared_from_this();
    try
    {
        std::thread([self, predicted] {
            try
            {
                Mtk::SetBacklight(predicted); // ① 寄存器直写，真正生效
                Mtk::RefreshPQ();             // ② 画质管线刷新
                Adb::Shell(L"settings put global picture_backlight " + std::to_wstring(predicted)); // ③ 记账
                Adb::Shell(L"settings put global xiaomi_picture_backlight " + std::to_wstring(predicted));
            }
            catch (...)
            {
            }
        }).detach();
    }
    catch (...)
    {
    }
}

void AppState::ToggleMute()
{
    auto self = shared_from_this();
    try
    {
        std::thread([self] {
            try
            {
                std::wstring command = L"input keyevent KEYCODE_VOLUME_MUTE";
                auto result = Adb::Exec({ L"shell", command });
                if ((!result || result->exitCode != 0) && Adb::Connect())
                {
                    Adb::Shell(command);
                }
            }
            catch (...)
            {
            }
        }).detach();
    }
    catch (...)
    {
    }
}
