#pragma once

#include <optional>
#include <string>
#include <utility>

struct SourceItem
{
    int id;
    wchar_t const* name;
};

// ID: 23=HDMI1 24=HDMI2 29=DP 30=USB-C（与 macOS 版一致）
inline constexpr SourceItem kSourceItems[] = {
    { 29, L"DP" },
    { 23, L"HDMI 1" },
    { 24, L"HDMI 2" },
    { 30, L"USB-C" },
};
inline constexpr size_t kSourceCount = sizeof(kSourceItems) / sizeof(kSourceItems[0]);

// MTK 寄存器通道（背光等真正生效的硬件写入）
namespace Mtk
{
    bool DeployJar();
    bool SetBacklight(int value);
    std::optional<int> ReadBacklight();
    void RefreshPQ();
}

// 音量直写通道（与小爱同学同一条 AudioManager 路径，瞬时一步到位）
namespace VolDirect
{
    bool DeployJar();
    bool Set(int value);
}

// 解析 "cmd media_session volume --stream 3 --get" 的输出
std::optional<std::pair<int, int>> QueryVolume();
