#include "pch.h"

#include "Adb.h"
#include "Config.h"
#include "Util.h"

#include <regex>

namespace
{
    std::mutex g_ioMutex;
    std::mutex g_binaryMutex;
    std::optional<std::wstring> g_cachedBinary;

    std::wstring QuoteArgument(std::wstring const& argument)
    {
        // MSVC 运行库的参数解析规则（与 CreateProcess 传参一致）
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        {
            return argument;
        }

        std::wstring quoted;
        quoted.push_back(L'"');
        for (auto it = argument.begin();; ++it)
        {
            size_t backslashes = 0;
            while (it != argument.end() && *it == L'\\')
            {
                ++it;
                ++backslashes;
            }
            if (it == argument.end())
            {
                quoted.append(backslashes * 2, L'\\');
                break;
            }
            if (*it == L'"')
            {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(L'"');
            }
            else
            {
                quoted.append(backslashes, L'\\');
                quoted.push_back(*it);
            }
        }
        quoted.push_back(L'"');
        return quoted;
    }

    std::wstring BuildCommandLine(std::wstring const& executable, std::vector<std::wstring> const& arguments)
    {
        std::wstring line = QuoteArgument(executable);
        for (auto const& argument : arguments)
        {
            line.push_back(L' ');
            line += QuoteArgument(argument);
        }
        return line;
    }

    void DrainPipe(HANDLE pipe, std::string& output)
    {
        char buffer[4096];
        for (;;)
        {
            DWORD read = 0;
            if (ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) == FALSE || read == 0)
            {
                break;
            }
            output.append(buffer, read);
        }
    }

    std::wstring Utf8ToWide(std::string const& text)
    {
        if (text.empty())
        {
            return {};
        }
        int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
        {
            return {};
        }
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    std::optional<std::wstring> FindAdb()
    {
        std::vector<std::wstring> candidates;

        // 1. 与 exe 放在一起的便携版 adb
        auto exeDirectory = Util::ExeDirectory();
        candidates.push_back(Util::JoinPath(exeDirectory, L"adb.exe"));

        // 2. PATH
        wchar_t pathBuffer[MAX_PATH * 4]{};
        DWORD found = SearchPathW(nullptr, L"adb.exe", nullptr, static_cast<DWORD>(std::size(pathBuffer)), pathBuffer, nullptr);
        if (found > 0 && found < std::size(pathBuffer))
        {
            candidates.emplace_back(pathBuffer, found);
        }

        // 3. 常见安装位置
        auto localAppData = Util::EnvironmentPath(L"LOCALAPPDATA");
        auto userProfile = Util::EnvironmentPath(L"USERPROFILE");
        auto programData = Util::EnvironmentPath(L"ProgramData");
        if (!localAppData.empty())
        {
            candidates.push_back(Util::JoinPath(localAppData, L"Android\\Sdk\\platform-tools\\adb.exe"));
            candidates.push_back(Util::JoinPath(localAppData, L"Microsoft\\WinGet\\Links\\adb.exe"));
        }
        if (!userProfile.empty())
        {
            auto scoop = Util::JoinPath(userProfile, L"scoop");
            auto apps = Util::JoinPath(scoop, L"apps");
            candidates.push_back(Util::JoinPath(scoop, L"shims\\adb.exe"));
            candidates.push_back(Util::JoinPath(apps, L"android-clt\\current\\platform-tools\\adb.exe"));
            candidates.push_back(Util::JoinPath(apps, L"android-platform-tools\\current\\platform-tools\\adb.exe"));
            candidates.push_back(Util::JoinPath(apps, L"adb\\current\\adb.exe"));
        }
        if (!programData.empty())
        {
            candidates.push_back(Util::JoinPath(programData, L"chocolatey\\bin\\adb.exe"));
        }
        candidates.emplace_back(L"C:\\platform-tools\\adb.exe");
        candidates.emplace_back(L"C:\\Android\\platform-tools\\adb.exe");

        for (auto const& candidate : candidates)
        {
            if (Util::FileExists(candidate))
            {
                return candidate;
            }
        }
        return std::nullopt;
    }
}

std::optional<std::wstring> Adb::Binary()
{
    std::lock_guard<std::mutex> lock(g_binaryMutex);
    if (g_cachedBinary)
    {
        return g_cachedBinary;
    }
    auto found = FindAdb();
    if (found)
    {
        g_cachedBinary = found;
    }
    return found;
}

void Adb::InvalidateBinary()
{
    std::lock_guard<std::mutex> lock(g_binaryMutex);
    g_cachedBinary.reset();
}

ProcResult Adb::Run(std::wstring const& executable, std::vector<std::wstring> const& arguments, int timeoutMs)
{
    ProcResult result;

    SECURITY_ATTRIBUTES attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (CreatePipe(&stdoutRead, &stdoutWrite, &attributes, 0) == FALSE)
    {
        return result;
    }
    if (CreatePipe(&stderrRead, &stderrWrite, &attributes, 0) == FALSE)
    {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return result;
    }
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;
    startup.hStdInput = nullptr;

    PROCESS_INFORMATION process{};
    std::wstring commandLine = BuildCommandLine(executable, arguments);
    BOOL created = CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process);

    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);

    if (created == FALSE)
    {
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        return result;
    }

    // 后台持续排空两个管道：子进程写满 64KB 缓冲会阻塞在 write 上导致永久死锁
    std::string outBytes;
    std::string errBytes;
    std::thread outReader([&outBytes, stdoutRead] { DrainPipe(stdoutRead, outBytes); });
    std::thread errReader([&errBytes, stderrRead] { DrainPipe(stderrRead, errBytes); });

    DWORD wait = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeoutMs > 0 ? timeoutMs : 1));
    bool timedOut = wait == WAIT_TIMEOUT;
    if (timedOut)
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    outReader.join();
    errReader.join();
    CloseHandle(stdoutRead);
    CloseHandle(stderrRead);

    if (timedOut)
    {
        // 与 macOS 版一致：超时视为失败且不返回任何输出
        result.exitCode = -1;
        result.output.clear();
        return result;
    }

    result.exitCode = static_cast<int>(exitCode);
    result.output = Util::Trim(Utf8ToWide(outBytes));
    return result;
}

std::optional<ProcResult> Adb::Exec(std::vector<std::wstring> const& arguments, int timeoutMs)
{
    auto binary = Binary();
    auto address = Config::DeviceAddress();
    if (!binary || !address)
    {
        return std::nullopt;
    }

    std::vector<std::wstring> full;
    full.reserve(arguments.size() + 2);
    full.push_back(L"-s");
    full.push_back(*address);
    full.insert(full.end(), arguments.begin(), arguments.end());

    std::lock_guard<std::mutex> lock(g_ioMutex);
    return Run(*binary, full, timeoutMs);
}

bool Adb::Connect()
{
    auto binary = Binary();
    auto address = Config::DeviceAddress();
    if (!binary || !address)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_ioMutex);
    Run(*binary, { L"connect", *address }, 3000);
    auto state = Run(*binary, { L"-s", *address, L"get-state" }, 3000);
    return state.output == L"device";
}

std::optional<std::wstring> Adb::Shell(std::wstring const& command, int timeoutMs)
{
    auto result = Exec({ L"shell", command }, timeoutMs);
    if (!result)
    {
        return std::nullopt;
    }
    return result->output;
}

bool Adb::Push(std::wstring const& localPath, std::wstring const& remotePath)
{
    auto result = Exec({ L"push", localPath, remotePath }, 10000);
    return result && result->exitCode == 0;
}

std::optional<std::wstring> Adb::TvServiceRun(std::vector<std::wstring> const& parts)
{
    std::wstring encoded;
    for (auto const& part : parts)
    {
        encoded += L"\\${IFS}";
        encoded += part;
    }

    std::wstring command = L"service call TvService 3 s16 \"sh -c eval";
    command += encoded;
    command += L"\"";
    return Shell(command);
}
