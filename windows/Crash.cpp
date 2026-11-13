#include "pch.h"

#include "Crash.h"
#include "Log.h"

#include <dbghelp.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    SRWLOCK g_lock = SRWLOCK_INIT;
    LONG g_handling = 0;
    LONG g_vectoredCount = 0;
    thread_local bool g_insideHandler = false;

    // 崩溃时不能再走 Log::Line 的互斥锁（可能正是持锁线程出错），这里直接写文件。
    void AppendRaw(std::wstring const& text)
    {
        wchar_t stamp[64]{};
        SYSTEMTIME now{};
        GetLocalTime(&now);
        swprintf_s(stamp, L"[%02d:%02d:%02d.%03d] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        std::wstring line = std::wstring(stamp) + text + L"\r\n";

        HANDLE file = CreateFileW(Log::Path().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        DWORD written = 0;
        WriteFile(file, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(file);
    }

    std::wstring Hex(DWORD64 value)
    {
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"0x%llX", static_cast<unsigned long long>(value));
        return buffer;
    }

    std::wstring ModuleFor(DWORD64 address)
    {
        HMODULE module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(address), &module) == FALSE || module == nullptr)
        {
            return L"<unknown>";
        }
        wchar_t path[MAX_PATH * 2]{};
        GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        std::wstring name = path;
        size_t slash = name.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            name = name.substr(slash + 1);
        }
        return name + L"+" + Hex(address - reinterpret_cast<DWORD64>(module));
    }

    std::wstring SymbolFor(HANDLE process, DWORD64 address)
    {
        constexpr size_t kMaxNameBytes = sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t);
        BYTE storage[kMaxNameBytes]{};
        auto symbol = reinterpret_cast<SYMBOL_INFOW*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
        symbol->MaxNameLen = 512;

        DWORD64 displacement = 0;
        if (SymFromAddrW(process, address, &displacement, symbol) != FALSE)
        {
            return std::wstring(symbol->Name, symbol->NameLen) + L"+" + Hex(displacement);
        }
        return ModuleFor(address);
    }

    void WriteMinidump(EXCEPTION_POINTERS* info)
    {
        wchar_t name[64]{};
        swprintf_s(name, L"crash-%lu.dmp", GetCurrentProcessId());
        std::wstring path = Log::Directory() + L"\\" + name;

        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        MINIDUMP_EXCEPTION_INFORMATION description{};
        description.ThreadId = GetCurrentThreadId();
        description.ExceptionPointers = info;
        description.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithThreadInfo, &description,
            nullptr, nullptr);
        CloseHandle(file);
        AppendRaw(L"minidump: " + path);
    }

    void WalkStack(EXCEPTION_POINTERS* info, HANDLE process, HANDLE thread)
    {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = info->ContextRecord->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = info->ContextRecord->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = info->ContextRecord->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        for (int index = 0; index < 48; ++index)
        {
            if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, info->ContextRecord, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == FALSE)
            {
                return;
            }
            if (frame.AddrPC.Offset == 0)
            {
                return;
            }
            wchar_t prefix[16]{};
            swprintf_s(prefix, L"  #%02d ", index);
            AppendRaw(std::wstring(prefix) + SymbolFor(process, frame.AddrPC.Offset));
        }
    }

    // StackWalk64 在部分系统模块里会失败，再抓一份原始返回地址做兜底。
    void WalkRawStack(HANDLE process)
    {
        void* frames[62]{};
        USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
        for (USHORT index = 0; index < count; ++index)
        {
            wchar_t prefix[16]{};
            swprintf_s(prefix, L"  ~%02u ", index);
            AppendRaw(std::wstring(prefix) + SymbolFor(process, reinterpret_cast<DWORD64>(frames[index])));
        }
    }

    void DumpException(EXCEPTION_POINTERS* info, wchar_t const* title)
    {
        AcquireSRWLockExclusive(&g_lock);
        AppendRaw(title);
        if (info != nullptr && info->ExceptionRecord != nullptr)
        {
            AppendRaw(L"code: " + Hex(info->ExceptionRecord->ExceptionCode) + L", flags: " +
                Hex(info->ExceptionRecord->ExceptionFlags) + L", address: " +
                ModuleFor(reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress)));
        }
        AppendRaw(L"thread: " + std::to_wstring(GetCurrentThreadId()));

        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        wchar_t executable[MAX_PATH * 2]{};
        GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        std::wstring directory = executable;
        size_t slash = directory.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            directory.resize(slash);
        }
        if (SymInitializeW(process, directory.c_str(), FALSE) != FALSE)
        {
            SymLoadModuleExW(process, nullptr, executable, nullptr,
                reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr)), 0, nullptr, 0);
            if (info != nullptr)
            {
                WalkStack(info, process, GetCurrentThread());
            }
            WalkRawStack(process);
            SymCleanup(process);
        }
        ReleaseSRWLockExclusive(&g_lock);
    }

    LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
    {
        if (InterlockedExchange(&g_handling, 1) != 0)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        DumpException(info, L"--- unhandled exception ---");
        WriteMinidump(info);
        AppendRaw(L"--- end of exception ---");

        // 交给系统按默认方式结束进程（保留 WER 记录）。
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // WinUI 运行时会替换进程级未处理异常过滤器，因此再挂一个向量化处理器，
    // 在第一次机会时记录故障点（代码、模块、调用栈）。
    LONG CALLBACK OnVectoredException(EXCEPTION_POINTERS* info)
    {
        if (info == nullptr || info->ExceptionRecord == nullptr || g_insideHandler)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        DWORD code = info->ExceptionRecord->ExceptionCode;
        constexpr DWORD kStowedException = 0xC000027Bu;
        if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
            code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
            code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != kStowedException)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (InterlockedIncrement(&g_vectoredCount) > 400)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        g_insideHandler = true;
        DumpException(info, L"--- first chance exception ---");
        g_insideHandler = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void OnTerminate()
    {
        AppendRaw(L"std::terminate called");
        std::abort();
    }

    void OnInvalidParameter(wchar_t const* expression, wchar_t const* function, wchar_t const* file,
        unsigned int line, uintptr_t)
    {
        wchar_t buffer[512]{};
        swprintf_s(buffer, L"invalid parameter: %s (%s) %s:%u", expression != nullptr ? expression : L"?",
            function != nullptr ? function : L"?", file != nullptr ? file : L"?", line);
        AppendRaw(buffer);
    }
}

void Crash::Install()
{
    SetUnhandledExceptionFilter(&OnUnhandledException);
    AddVectoredExceptionHandler(1, &OnVectoredException);
    std::set_terminate(&OnTerminate);
    _set_invalid_parameter_handler(&OnInvalidParameter);
}
