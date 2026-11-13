#include "pch.h"

#include "Config.h"
#include "Util.h"

namespace
{
    constexpr wchar_t kRegistryPath[] = L"Software\\Wuhan\\MonitorSwitch";
    constexpr wchar_t kValueName[] = L"DeviceAddress";
}

std::optional<std::wstring> Config::DeviceAddress()
{
    wchar_t buffer[1024]{};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRegistryPath, kValueName, RRF_RT_REG_SZ, &type, buffer, &size);
    if (status != ERROR_SUCCESS)
    {
        return std::nullopt;
    }
    std::wstring value(buffer, size / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }
    if (value.empty())
    {
        return std::nullopt;
    }
    return value;
}

bool Config::SetDeviceAddress(std::optional<std::wstring> const& address)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }
    LSTATUS status;
    if (address)
    {
        status = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<BYTE const*>(address->c_str()),
            static_cast<DWORD>((address->size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

std::optional<std::wstring> Config::Normalize(std::wstring const& raw)
{
    auto address = Util::Trim(raw);
    // 此设备使用 IPv4 网络 ADB；整串检查，避免粘贴的垃圾字符被静默忽略。
    auto colon = address.find(L':');
    auto host = address.substr(0, colon);
    auto portText = colon == std::wstring::npos ? L"5555" : address.substr(colon + 1);
    auto digits = [](std::wstring const& text) {
        return !text.empty() && text.find_first_not_of(L"0123456789") == std::wstring::npos;
    };
    if (!digits(portText) || portText.size() > 5) return std::nullopt;
    auto port = Util::ParseInt(portText);
    if (!port || *port < 1 || *port > 65535) return std::nullopt;

    size_t begin = 0;
    std::wstring normalized;
    for (int octet = 0; octet < 4; ++octet)
    {
        auto end = host.find(L'.', begin);
        if ((octet < 3) != (end != std::wstring::npos)) return std::nullopt;
        auto part = host.substr(begin, end == std::wstring::npos ? end : end - begin);
        if (!digits(part) || part.size() > 3) return std::nullopt;
        auto value = Util::ParseInt(part);
        if (!value || *value > 255) return std::nullopt;
        if (octet != 0) normalized += L'.';
        normalized += std::to_wstring(*value);
        begin = end + 1;
    }
    return normalized + L":" + std::to_wstring(*port);
}
