#Requires -Version 5.1
<#
.SYNOPSIS
    构建 MonitorSwitch（Windows / C++ + WinUI 3）。
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("x64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $root "MonitorSwitch.vcxproj"

if (-not (Test-Path (Join-Path $root "Assets\AppIcon.ico"))) {
    Write-Host "生成应用图标…"
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        & $python.Source (Join-Path $root "tools\make_icon.py")
    }
    else {
        throw "缺少 Assets\AppIcon.ico（可用 python tools/make_icon.py 生成）"
    }
}

$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
        Select-Object -First 1
}
if (-not $msbuild -or -not (Test-Path $msbuild)) {
    throw "未找到 MSBuild，请先安装 Visual Studio 2022（勾选「使用 C++ 的桌面开发」）"
}

Write-Host "MSBuild: $msbuild"
& $msbuild $project /restore /m /nologo "/p:Configuration=$Configuration" "/p:Platform=$Platform" /v:minimal
if ($LASTEXITCODE -ne 0) {
    throw "构建失败，退出码 $LASTEXITCODE"
}

$exe = Join-Path $root "build\$Configuration\MonitorSwitch.exe"
if (-not (Test-Path $exe)) {
    throw "未生成 $exe"
}
Write-Host ""
Write-Host "构建完成: $exe" -ForegroundColor Green
