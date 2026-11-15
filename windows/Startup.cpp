#include "Startup.h"

#include <windows.h>
#include <string>

namespace
{
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kValueName[] = L"MonitorSwitch";
}

std::optional<bool> Startup::IsEnabled()
{
    wchar_t command[MAX_PATH + 1]{};
    DWORD size = sizeof(command);
    auto status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
        nullptr, command, &size);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return false;
    if (status != ERROR_SUCCESS) return std::nullopt;
    return command[0] != L'\0';
}

bool Startup::SetEnabled(bool enabled)
{
    std::wstring command;
    if (enabled)
    {
        wchar_t path[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return false;
        command = L"\"" + std::wstring(path, length) + L"\"";
        // Windows documents a maximum command-line length of 260 for Run values.
        if (command.size() >= MAX_PATH) return false;
    }

    HKEY key = nullptr;
    auto status = enabled
        ? RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (!enabled && (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)) return true;
    if (status != ERROR_SUCCESS) return false;

    if (enabled)
    {
        status = RegSetValueExW(key, kValueName, 0, REG_SZ,
            reinterpret_cast<BYTE const*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
