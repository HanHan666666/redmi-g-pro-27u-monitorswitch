#pragma once

// 未处理异常诊断：把异常代码、故障线程、故障模块和调用栈写入 app.log，
// 并在本地生成 minidump，便于反馈问题时定位崩溃点。
namespace Crash
{
    void Install();
}
