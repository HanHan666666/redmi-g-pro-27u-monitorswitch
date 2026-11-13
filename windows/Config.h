#pragma once

#include <optional>
#include <string>

// 设备地址配置。macOS 版本存 UserDefaults，Windows 版存
// HKCU\Software\Wuhan\MonitorSwitch（DeviceAddress），代码里不写死任何地址。
namespace Config
{
    std::optional<std::wstring> DeviceAddress();
    bool SetDeviceAddress(std::optional<std::wstring> const& address);

    // 允许输入裸 IP，自动补 :5555；非法输入返回 nullopt
    std::optional<std::wstring> Normalize(std::wstring const& raw);
}
