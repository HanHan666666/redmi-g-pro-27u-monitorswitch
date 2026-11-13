// 临时调试工具：极简调试器，用来抓 WinUI 的 stowed exception 文本与调用栈。
// 用法：minidbg.exe <exe 完整路径> [日志路径]
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

namespace
{
    HANDLE g_process = nullptr;
    DWORD g_processId = 0;
    FILE* g_log = nullptr;
    int g_cppLogged = 0;

    void Log(char const* format, ...)
    {
        if (g_log == nullptr)
        {
            return;
        }
        va_list args;
        va_start(args, format);
        vfprintf(g_log, format, args);
        va_end(args);
        fflush(g_log);
    }

    std::string Narrow(std::wstring const& text)
    {
        if (text.empty())
        {
            return {};
        }
        int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), needed, nullptr, nullptr);
        return result;
    }

    struct ModuleInfo
    {
        DWORD64 base;
        DWORD size;
        std::wstring name;
    };

    std::vector<ModuleInfo> g_modules;

    void RefreshModules()
    {
        g_modules.clear();
        HMODULE handles[1024]{};
        DWORD needed = 0;
        if (EnumProcessModulesEx(g_process, handles, sizeof(handles), &needed, LIST_MODULES_ALL) == FALSE)
        {
            return;
        }
        size_t count = needed / sizeof(HMODULE);
        for (size_t index = 0; index < count; ++index)
        {
            MODULEINFO info{};
            if (GetModuleInformation(g_process, handles[index], &info, sizeof(info)) == FALSE)
            {
                continue;
            }
            wchar_t path[MAX_PATH * 2]{};
            if (GetModuleFileNameExW(g_process, handles[index], path, static_cast<DWORD>(std::size(path))) == 0)
            {
                continue;
            }
            std::wstring name = path;
            size_t slash = name.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
            {
                name = name.substr(slash + 1);
            }
            g_modules.push_back({ reinterpret_cast<DWORD64>(info.lpBaseOfDll), info.SizeOfImage, name });
        }
    }

    std::string DescribeAddress(DWORD64 address)
    {
        for (auto const& module : g_modules)
        {
            if (address >= module.base && address < module.base + module.size)
            {
                char buffer[256]{};
                sprintf_s(buffer, "%s+0x%llX", Narrow(module.name).c_str(),
                    static_cast<unsigned long long>(address - module.base));
                return buffer;
            }
        }
        char buffer[64]{};
        sprintf_s(buffer, "0x%llX", static_cast<unsigned long long>(address));
        return buffer;
    }

    void LogStack(HANDLE thread, CONTEXT const& context, int limit)
    {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        CONTEXT mutableContext = context;
        for (int index = 0; index < limit; ++index)
        {
            if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_process, thread, &frame, &mutableContext, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == FALSE)
            {
                break;
            }
            if (frame.AddrPC.Offset == 0)
            {
                break;
            }
            Log("  #%02d %s\n", index, DescribeAddress(frame.AddrPC.Offset).c_str());
        }
    }

    bool ReadMemory(DWORD64 address, void* buffer, size_t size)
    {
        SIZE_T read = 0;
        return ReadProcessMemory(g_process, reinterpret_cast<LPCVOID>(address), buffer, size, &read) != FALSE && read == size;
    }

#pragma pack(push, 4)
    struct StowedHeader
    {
        ULONG Signature;
        ULONG Size;
        HRESULT ResultCode;
        DWORD FormAndThread;
        DWORD_PTR ExceptionAddress;   // form 0
        ULONG StackTraceWordSize;
        ULONG StackTraceWords;
        DWORD_PTR StackTrace;
        // form 1: DWORD_PTR ErrorText at ExceptionAddress offset
        ULONG NestedExceptionType;
        DWORD_PTR NestedException;
    };
#pragma pack(pop)

    void DecodeStowedException(EXCEPTION_POINTERS const& pointers, HANDLE thread)
    {
        auto record = pointers.ExceptionRecord;
        if (record->NumberParameters < 2)
        {
            Log("  (no stowed parameters)\n");
            return;
        }
        auto arrayAddress = static_cast<DWORD64>(record->ExceptionInformation[0]);
        auto count = static_cast<DWORD>(record->ExceptionInformation[1]);
        Log("  stowed count=%lu array=0x%llX\n", count, static_cast<unsigned long long>(arrayAddress));

        for (DWORD index = 0; index < count && index < 8; ++index)
        {
            DWORD64 entryAddress = 0;
            if (ReadMemory(arrayAddress + index * sizeof(DWORD64), &entryAddress, sizeof(entryAddress)) == false ||
                entryAddress == 0)
            {
                Log("  entry %lu: unreadable\n", index);
                continue;
            }
            StowedHeader header{};
            if (ReadMemory(entryAddress, &header, sizeof(header)) == false)
            {
                Log("  entry %lu: header unreadable\n", index);
                continue;
            }
            DWORD form = header.FormAndThread & 0x3;
            DWORD threadId = header.FormAndThread >> 2;
            Log("  entry %lu: sig=0x%lX size=%lu hr=0x%08lX form=%lu tid=%lu\n", index, header.Signature,
                header.Size, static_cast<unsigned long>(header.ResultCode), form, threadId);

            if (form == 1)
            {
                wchar_t textBuffer[1024]{};
                if (ReadMemory(header.ExceptionAddress, textBuffer, sizeof(textBuffer) - sizeof(wchar_t)))
                {
                    textBuffer[(std::size(textBuffer) - 1)] = L'\0';
                    Log("    text: %s\n", Narrow(textBuffer).c_str());
                }
                else
                {
                    Log("    text unreadable\n");
                }
            }
            else
            {
                Log("    address: %s\n", DescribeAddress(header.ExceptionAddress).c_str());
                Log("    words: size=%lu count=%lu\n", header.StackTraceWordSize, header.StackTraceWords);
                if (header.StackTraceWordSize == sizeof(DWORD64))
                {
                    std::vector<DWORD64> words(header.StackTraceWords);
                    if (!words.empty() &&
                        ReadMemory(header.StackTrace, words.data(), words.size() * sizeof(DWORD64)))
                    {
                        for (size_t word = 0; word < words.size() && word < 32; ++word)
                        {
                            Log("      [%02zu] %s\n", word, DescribeAddress(words[word]).c_str());
                        }
                    }
                }
            }

            if (header.NestedExceptionType != 0 && header.NestedException != 0)
            {
                Log("    nested type=0x%lX ptr=0x%llX\n", header.NestedExceptionType,
                    static_cast<unsigned long long>(header.NestedException));
            }
        }
        (void)thread;
    }

    bool StackTouchesUi(EXCEPTION_POINTERS const& pointers, HANDLE thread)
    {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = pointers.ContextRecord->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = pointers.ContextRecord->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = pointers.ContextRecord->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        CONTEXT context = *pointers.ContextRecord;
        for (int index = 0; index < 40; ++index)
        {
            if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_process, thread, &frame, &context, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == FALSE)
            {
                break;
            }
            if (frame.AddrPC.Offset == 0)
            {
                break;
            }
            std::string name = DescribeAddress(frame.AddrPC.Offset);
            if (name.find("Microsoft.UI.Xaml") != std::string::npos || name.find("WinUIEdit") != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
}

int wmain(int argc, wchar_t** argv)
{
    fwprintf(stderr, L"entry argc=%d\n", argc); fflush(stderr);
    if (argc < 2)
    {
        wprintf(L"usage: minidbg <exe> [log]\n");
        return 2;
    }
    std::wstring exe = argv[1];
    std::wstring logPath = argc >= 3 ? argv[2] : (exe + L".minidbg.log");

    fwprintf(stderr, L"step: open log\n"); fflush(stderr);
    _wfopen_s(&g_log, logPath.c_str(), L"w, ccs=UTF-8");
    if (g_log == nullptr)
    {
        wprintf(L"cannot open log\n");
        return 3;
    }

    fwprintf(stderr, L"step: build cmdline\n"); fflush(stderr);
    std::wstring commandLine = L"\"" + exe + L"\"";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    fwprintf(stderr, L"step: create process\n"); fflush(stderr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(exe.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
            DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process) == FALSE)
    {
        Log("CreateProcess failed: %lu\n", GetLastError());
        return 4;
    }
    g_process = process.hProcess;
    g_processId = process.dwProcessId;
    CloseHandle(process.hThread);
    Log("create process ok pid=%lu\n", g_processId);

    Log("debugger started\n");
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    wchar_t exePath[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    std::wstring symbolPath = exePath;
    size_t slash = symbolPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        symbolPath.resize(slash);
    }
    Log("symbols init at %s\n", Narrow(symbolPath).c_str());
    if (SymInitializeW(g_process, symbolPath.c_str(), FALSE) == FALSE)
    {
        Log("SymInitialize failed %lu\n", GetLastError());
    }
    Log("symbols ready\n");

    bool done = false;
    bool sawInitialBreakpoint = false;
    while (!done)
    {
        DEBUG_EVENT event{};
        if (WaitForDebugEvent(&event, 20000) == FALSE)
        {
            Log("WaitForDebugEvent timeout/failure: %lu\n", GetLastError());
            break;
        }

        DWORD continueStatus = DBG_EXCEPTION_NOT_HANDLED;
        switch (event.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (event.u.CreateProcessInfo.hFile != nullptr && event.u.CreateProcessInfo.hFile != INVALID_HANDLE_VALUE)
            {
                CloseHandle(event.u.CreateProcessInfo.hFile);
            }
            RefreshModules();
            Log("process started pid=%lu\n", event.dwProcessId);
            break;

        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile != nullptr && event.u.LoadDll.hFile != INVALID_HANDLE_VALUE)
            {
                CloseHandle(event.u.LoadDll.hFile);
            }
            RefreshModules();
            break;

        case EXCEPTION_DEBUG_EVENT:
        {
            auto& record = event.u.Exception.ExceptionRecord;
            DWORD code = record.ExceptionCode;
            HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE,
                event.dwThreadId);
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            bool haveContext = thread != nullptr && GetThreadContext(thread, &context) != FALSE;

            if (code == EXCEPTION_BREAKPOINT && !sawInitialBreakpoint)
            {
                sawInitialBreakpoint = true;
                continueStatus = DBG_CONTINUE;
                break;
            }

            bool interesting = code == 0xC000027Bu || code == EXCEPTION_ACCESS_VIOLATION || code == 0xC0000409u;
            if (!interesting && code == 0xE06D7363u && g_cppLogged < 12 && event.u.Exception.dwFirstChance)
            {
                if (haveContext)
                {
                    EXCEPTION_POINTERS pointers{ &record, &context };
                    if (StackTouchesUi(pointers, thread))
                    {
                        interesting = true;
                        ++g_cppLogged;
                    }
                }
            }

            if (interesting)
            {
                Log("=== exception 0x%08lX pid=%lu tid=%lu first=%d addr=%s\n", code, event.dwProcessId,
                    event.dwThreadId, event.u.Exception.dwFirstChance,
                    DescribeAddress(reinterpret_cast<DWORD64>(record.ExceptionAddress)).c_str());
                if (haveContext)
                {
                    LogStack(thread, context, 40);
                }
                if (code == 0xC000027Bu && haveContext)
                {
                    EXCEPTION_POINTERS pointers{ &record, &context };
                    DecodeStowedException(pointers, thread);
                }
            }

            if (thread != nullptr)
            {
                CloseHandle(thread);
            }
            break;
        }

        case EXIT_PROCESS_DEBUG_EVENT:
            Log("process exited code=0x%08lX\n", static_cast<unsigned long>(event.u.ExitProcess.dwExitCode));
            done = true;
            break;

        case OUTPUT_DEBUG_STRING_EVENT:
        {
            auto const& info = event.u.DebugString;
            if (info.nDebugStringLength > 0 && info.nDebugStringLength < 4096)
            {
                std::vector<char> buffer(info.nDebugStringLength + 1, '\0');
                SIZE_T read = 0;
                if (ReadProcessMemory(g_process, info.lpDebugStringData, buffer.data(), info.nDebugStringLength, &read))
                {
                    Log("debug: %s\n", buffer.data());
                }
            }
            break;
        }

        default:
            break;
        }

        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continueStatus);
    }

    WaitForSingleObject(g_process, 5000);
    SymCleanup(g_process);
    CloseHandle(g_process);
    fclose(g_log);
    return 0;
}
