#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../Startup.h"

namespace
{
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    void Require(bool condition, char const* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Override HKCU for this process only; tests never write a real startup entry.
    class RegistrySandbox
    {
    public:
        RegistrySandbox()
        {
            m_path = L"Software\\Wuhan\\MonitorSwitch.Tests\\Startup-" + std::to_wstring(GetCurrentProcessId())
                + L"-" + std::to_wstring(GetTickCount64());
            Require(RegCreateKeyExW(HKEY_CURRENT_USER, m_path.c_str(), 0, nullptr, 0,
                KEY_ALL_ACCESS, nullptr, &m_key, nullptr) == ERROR_SUCCESS, "Create registry sandbox");
            if (RegOverridePredefKey(HKEY_CURRENT_USER, m_key) != ERROR_SUCCESS)
            {
                RegCloseKey(m_key);
                RegDeleteTreeW(HKEY_CURRENT_USER, m_path.c_str());
                throw std::runtime_error("Redirect HKCU to sandbox");
            }
        }
        ~RegistrySandbox()
        {
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            RegCloseKey(m_key);
            RegDeleteTreeW(HKEY_CURRENT_USER, m_path.c_str());
        }
    private:
        std::wstring m_path;
        HKEY m_key{};
    };
}

int main()
{
    try
    {
        RegistrySandbox sandbox;
        Require(Startup::IsEnabled() == false, "Disabled when Run key is absent");
        Require(Startup::SetEnabled(false), "Disabling an absent entry succeeds");
        Require(Startup::SetEnabled(true), "Enable startup");
        Require(Startup::IsEnabled() == true, "Read enabled state from Windows");

        wchar_t executable[MAX_PATH]{};
        Require(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "Get executable path");
        std::wstring expected = L"\"" + std::wstring(executable) + L"\"";
        wchar_t command[MAX_PATH]{};
        DWORD size = sizeof(command);
        DWORD type{};
        Require(RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"MonitorSwitch", RRF_RT_REG_SZ,
            &type, command, &size) == ERROR_SUCCESS, "Read registered command");
        Require(type == REG_SZ && command == expected, "Register the full quoted executable path");
        Require(Startup::SetEnabled(true), "Enabling twice succeeds");

        HKEY key{};
        Require(RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS, "Open Run key");
        DWORD marker = 123;
        auto unrelatedStatus = RegSetValueExW(key, L"UnrelatedApp", 0, REG_DWORD,
            reinterpret_cast<BYTE const*>(&marker), sizeof(marker));
        RegCloseKey(key);
        Require(unrelatedStatus == ERROR_SUCCESS, "Create unrelated startup entry");
        Require(Startup::SetEnabled(false), "Disable startup");
        Require(Startup::IsEnabled() == false, "Read disabled state");
        Require(Startup::SetEnabled(false), "Disabling twice succeeds");
        size = sizeof(marker);
        Require(RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"UnrelatedApp", RRF_RT_REG_DWORD,
            nullptr, &marker, &size) == ERROR_SUCCESS && marker == 123, "Preserve unrelated startup entries");

        Require(RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS, "Open Run key again");
        auto malformedStatus = RegSetValueExW(key, L"MonitorSwitch", 0, REG_DWORD,
            reinterpret_cast<BYTE const*>(&marker), sizeof(marker));
        RegCloseKey(key);
        Require(malformedStatus == ERROR_SUCCESS, "Create malformed registration");
        Require(!Startup::IsEnabled().has_value(), "Report malformed registration as an error");
        Require(Startup::SetEnabled(true) && Startup::IsEnabled() == true, "Replace malformed registration");
        std::cout << "Startup registration tests passed.\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
