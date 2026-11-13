// 临时调试工具：把 RVA 解析成 PDB 符号，用于定位崩溃栈。
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#pragma comment(lib, "dbghelp.lib")

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3)
    {
        wprintf(L"usage: symresolve <exe> <rva-hex> [...]\n");
        return 2;
    }

    std::wstring exe = argv[1];
    HANDLE file = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        wprintf(L"cannot open %s\n", exe.c_str());
        return 3;
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    auto base = static_cast<const BYTE*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    DWORD64 imageBase = nt->OptionalHeader.ImageBase;
    wprintf(L"image base 0x%llX\n", imageBase);

    std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    HANDLE process = GetCurrentProcess();
    if (!SymInitializeW(process, dir.c_str(), FALSE))
    {
        wprintf(L"SymInitialize failed: %lu\n", GetLastError());
        return 4;
    }
    if (!SymLoadModuleExW(process, nullptr, exe.c_str(), nullptr, imageBase, nt->OptionalHeader.SizeOfImage, nullptr, 0))
    {
        wprintf(L"SymLoadModuleEx failed: %lu\n", GetLastError());
    }

    for (int index = 2; index < argc; ++index)
    {
        DWORD64 rva = wcstoull(argv[index], nullptr, 16);
        BYTE storage[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)]{};
        auto symbol = reinterpret_cast<SYMBOL_INFOW*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
        symbol->MaxNameLen = 512;
        DWORD64 displacement = 0;
        if (SymFromAddrW(process, imageBase + rva, &displacement, symbol))
        {
            wprintf(L"%08llX  %s+0x%llX\n", static_cast<unsigned long long>(rva), symbol->Name,
                static_cast<unsigned long long>(displacement));
        }
        else
        {
            wprintf(L"%08llX  <no symbol> err %lu\n", static_cast<unsigned long long>(rva), GetLastError());
        }
    }
    SymCleanup(process);
    UnmapViewOfFile(base);
    CloseHandle(mapping);
    CloseHandle(file);
    return 0;
}
