#include "pch.h"

#include "Device.h"
#include "Adb.h"
#include "Util.h"

#include <regex>

namespace
{
    constexpr wchar_t kCacheDir[] = L"/data/data/mitv.service/cache";
    constexpr wchar_t kMtkJarName[] = L"MtkDirectTool.jar";
    constexpr wchar_t kVolJarName[] = L"VolDirectTool.jar";
    constexpr wchar_t kMtkJarPath[] = L"/data/data/mitv.service/cache/MtkDirectTool.jar";
    constexpr wchar_t kVolJarPath[] = L"/data/data/mitv.service/cache/VolDirectTool.jar";
    constexpr wchar_t kBacklightKey[] = L"g_disp__disp_back_light";
    constexpr wchar_t kProcessPath[] = L"/system/bin/app_process";

    std::wstring FindAsset(std::wstring const& name)
    {
        auto exeDirectory = Util::ExeDirectory();
        auto direct = Util::JoinPath(exeDirectory, name);
        if (Util::FileExists(direct))
        {
            return direct;
        }
        auto nested = Util::JoinPath(Util::JoinPath(exeDirectory, L"Assets"), name);
        if (Util::FileExists(nested))
        {
            return nested;
        }
        return {};
    }

    // 先 push 到 /sdcard，再借 TvService 系统权限复制进 mitv.service 私有目录
    bool DeployJarFile(std::wstring const& jarName, std::wstring const& remotePath)
    {
        auto local = FindAsset(jarName);
        if (local.empty())
        {
            return false;
        }
        std::wstring staged = std::wstring(L"/sdcard/") + jarName;
        if (!Adb::Push(local, staged))
        {
            return false;
        }
        Adb::TvServiceRun({ L"cp", staged, remotePath });
        return true;
    }

    std::optional<std::pair<int, int>> ParseVolume(std::wstring const& output)
    {
        // "[V] volume is 18 in range [0..100]"
        static const std::wregex pattern(L"volume is (\\d+) in range \\[0\\.\\.(\\d+)\\]");
        std::wsmatch match;
        if (!std::regex_search(output, match, pattern))
        {
            return std::nullopt;
        }
        return std::make_pair(std::stoi(match[1].str()), std::stoi(match[2].str()));
    }
}

bool Mtk::DeployJar()
{
    return DeployJarFile(kMtkJarName, kMtkJarPath);
}

bool Mtk::SetBacklight(int value)
{
    auto result = Adb::TvServiceRun({
        std::wstring(L"CLASSPATH=") + kMtkJarPath,
        kProcessPath,
        kCacheDir,
        L"MtkDirectTool",
        L"set",
        kBacklightKey,
        std::to_wstring(value),
        L"3",
    });
    return result.has_value();
}

std::optional<int> Mtk::ReadBacklight()
{
    Adb::Shell(L"logcat -c");
    Adb::TvServiceRun({
        std::wstring(L"CLASSPATH=") + kMtkJarPath,
        kProcessPath,
        kCacheDir,
        L"MtkDirectTool",
        L"get",
        kBacklightKey,
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto log = Adb::Shell(L"logcat -d | grep 'GET g_disp__disp_back_light' | tail -1");
    if (!log)
    {
        return std::nullopt;
    }
    auto marker = log->find(L"= ");
    if (marker == std::wstring::npos)
    {
        return std::nullopt;
    }
    return Util::ParseInt(log->substr(marker + 2));
}

void Mtk::RefreshPQ()
{
    Adb::Shell(L"am broadcast -a com.xiaomi.mitv.action.PIC_MODE_CHANGED --ei picmode 7");
}

bool VolDirect::DeployJar()
{
    return DeployJarFile(kVolJarName, kVolJarPath);
}

bool VolDirect::Set(int value)
{
    auto result = Adb::TvServiceRun({
        std::wstring(L"CLASSPATH=") + kVolJarPath,
        kProcessPath,
        kCacheDir,
        L"VolDirectTool",
        L"set",
        L"3",
        std::to_wstring(value),
    });
    return result.has_value();
}

std::optional<std::pair<int, int>> QueryVolume()
{
    auto output = Adb::Shell(L"cmd media_session volume --stream 3 --get");
    if (!output)
    {
        return std::nullopt;
    }
    return ParseVolume(*output);
}
