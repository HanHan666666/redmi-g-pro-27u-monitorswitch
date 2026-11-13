#pragma once

#include <windows.h>

#include <mutex>
#include <string>

// 简单的诊断日志（%LOCALAPPDATA%\MonitorSwitch\app.log）。发布版也会写入，
// 方便用户反馈问题时提供现场。
namespace Log
{
    inline std::wstring Directory()
    {
        wchar_t buffer[MAX_PATH * 2]{};
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
        std::wstring directory = length > 0 ? std::wstring(buffer, length) : std::wstring(L".");
        directory += L"\\MonitorSwitch";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory;
    }

    inline std::wstring Path()
    {
        return Directory() + L"\\app.log";
    }

    inline void Clear()
    {
        DeleteFileW(Path().c_str());
    }

    inline void Line(std::wstring const& text)
    {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);

        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t stamp[64]{};
        swprintf_s(stamp, L"[%02d:%02d:%02d.%03d] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        std::wstring line = std::wstring(stamp) + text + L"\r\n";
        HANDLE file = CreateFileW(Path().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
            CloseHandle(file);
        }
    }
}
