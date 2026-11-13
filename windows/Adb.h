#pragma once

#include <optional>
#include <string>
#include <vector>

struct ProcResult
{
    int exitCode{ -1 };
    std::wstring output; // 去掉首尾空白的 stdout
};

// 与 macOS 版一一对应的 adb 封装：
//   * 所有设备 I/O 串行执行（长指令不会与轮询交错）
//   * 每次调用都有超时，超时直接杀进程（adb 碰上网络抖动可能无限挂起）
namespace Adb
{
    std::optional<std::wstring> Binary();
    void InvalidateBinary();

    ProcResult Run(std::wstring const& executable, std::vector<std::wstring> const& arguments, int timeoutMs);

    std::optional<ProcResult> Exec(std::vector<std::wstring> const& arguments, int timeoutMs = 3000);
    bool Connect();
    std::optional<std::wstring> Shell(std::wstring const& command, int timeoutMs = 3000);
    bool Push(std::wstring const& localPath, std::wstring const& remotePath);

    // 借 TvService（系统权限）执行命令。parts 中的空格以字面量 ${IFS} 编码，
    // 由设备端 sh -c eval 展开——这是 TvService runSystemCommand 的调用约定
    // （提取自 Mimonitor_Toolbox adb.py，勿改成真实空格，会静默失败）。
    std::optional<std::wstring> TvServiceRun(std::vector<std::wstring> const& parts);
}
