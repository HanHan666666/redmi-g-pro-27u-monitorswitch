#pragma once

#include <windows.h>

#include <iterator>
#include <optional>
#include <string>

namespace Util
{
    inline std::wstring Trim(std::wstring const& value)
    {
        size_t begin = 0;
        size_t end = value.size();
        while (begin < end && iswspace(value[begin]) != 0)
        {
            ++begin;
        }
        while (end > begin && iswspace(value[end - 1]) != 0)
        {
            --end;
        }
        return value.substr(begin, end - begin);
    }

    // 与 Swift 的 Int(String) 行为一致：整串必须是十进制整数
    inline std::optional<int> ParseInt(std::wstring const& value)
    {
        auto text = Trim(value);
        if (text.empty())
        {
            return std::nullopt;
        }
        size_t index = 0;
        bool negative = false;
        if (text[0] == L'-' || text[0] == L'+')
        {
            negative = text[0] == L'-';
            index = 1;
        }
        if (index >= text.size())
        {
            return std::nullopt;
        }
        long long number = 0;
        for (; index < text.size(); ++index)
        {
            if (text[index] < L'0' || text[index] > L'9')
            {
                return std::nullopt;
            }
            number = number * 10 + (text[index] - L'0');
            if (number > 100000000)
            {
                return std::nullopt;
            }
        }
        return static_cast<int>(negative ? -number : number);
    }

    inline std::wstring ExeDirectory()
    {
        wchar_t buffer[MAX_PATH * 4]{};
        DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length == 0 || length >= std::size(buffer))
        {
            return L".";
        }
        std::wstring path(buffer, length);
        auto slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"." : path.substr(0, slash);
    }

    inline std::wstring EnvironmentPath(wchar_t const* name)
    {
        wchar_t buffer[MAX_PATH * 4]{};
        DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
        return length == 0 || length >= std::size(buffer) ? std::wstring{} : std::wstring(buffer, length);
    }

    inline bool FileExists(std::wstring const& path)
    {
        if (path.empty())
        {
            return false;
        }
        DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    inline std::wstring JoinPath(std::wstring const& directory, std::wstring const& name)
    {
        if (directory.empty())
        {
            return name;
        }
        wchar_t last = directory.back();
        if (last == L'\\' || last == L'/')
        {
            return directory + name;
        }
        return directory + L"\\" + name;
    }
}
