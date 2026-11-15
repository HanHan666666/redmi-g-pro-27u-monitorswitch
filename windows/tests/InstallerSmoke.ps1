#Requires -Version 7.0
param(
    [Parameter(Mandatory)] [string]$SetupPath
)

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'Run this installation/uninstallation smoke test only on a disposable GitHub Actions runner.'
}

function Invoke-Installer([string]$Executable, [string[]]$Arguments) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    foreach ($argument in $Arguments) { $startInfo.ArgumentList.Add($argument) }
    $installer = [Diagnostics.Process]::Start($startInfo)
    if (-not $installer.WaitForExit(120000)) {
        $installer.Kill($true)
        throw 'The installer timed out.'
    }
    if ($installer.ExitCode -ne 0) { throw "Installer exit code: $($installer.ExitCode)" }
}

$installDir = Join-Path $env:RUNNER_TEMP 'MonitorSwitch install test'
$logDir = Join-Path $env:RUNNER_TEMP 'MonitorSwitch-installer-logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$installArgs = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$installDir")
Invoke-Installer $SetupPath ($installArgs + "/LOG=$logDir\install.log")
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$startupCommand = '"' + (Join-Path $installDir 'MonitorSwitch.exe') + '"'
if (-not (Test-Path -LiteralPath $runKey)) { New-Item -Path $runKey -Force | Out-Null }
Set-ItemProperty -LiteralPath $runKey -Name MonitorSwitch -Value $startupCommand -Type String
# Installing a second time must update the same installation successfully.
Invoke-Installer $SetupPath ($installArgs + "/LOG=$logDir\upgrade.log")
if ((Get-ItemPropertyValue -LiteralPath $runKey -Name MonitorSwitch) -ne $startupCommand) {
    throw 'Upgrade changed the startup preference.'
}

foreach ($name in @('MonitorSwitch.exe', 'unins000.exe', 'Microsoft.ui.xaml.dll', 'vcruntime140.dll', 'MtkDirectTool.jar', 'VolDirectTool.jar')) {
    if (-not (Test-Path -LiteralPath (Join-Path $installDir $name))) { throw "Installed payload is missing $name." }
}

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = Join-Path $installDir 'MonitorSwitch.exe'
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.Environment['MONITORSWITCH_NO_AUTO_HIDE'] = '1'
$application = [Diagnostics.Process]::Start($startInfo)
$appLog = Join-Path $env:LOCALAPPDATA 'MonitorSwitch\app.log'
try {
    Start-Sleep -Seconds 8
    if ($application.HasExited) { throw "MonitorSwitch exited during startup: $($application.ExitCode)" }
    $text = Get-Content -LiteralPath $appLog -Raw -Encoding unicode
    if ($text -notmatch 'panel created' -or $text -match 'UnhandledException|OnLaunched hresult_error|OnLaunched unknown exception') {
        throw 'The installed WinUI panel did not start successfully. See app.log.'
    }
}
finally {
    if (-not $application.HasExited) { $application.Kill($true); $application.WaitForExit() }
    if (Test-Path -LiteralPath $appLog) { Copy-Item -LiteralPath $appLog -Destination $logDir }
}

Invoke-Installer (Join-Path $installDir 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$logDir\uninstall.log")
if (Test-Path -LiteralPath (Join-Path $installDir 'MonitorSwitch.exe')) { throw 'Uninstall left the application executable behind.' }
if (Get-ItemProperty -LiteralPath $runKey -Name MonitorSwitch -ErrorAction SilentlyContinue) {
    throw 'Uninstall left its startup entry behind.'
}

# A portable copy can own the preference independently of this installation.
# Use a fresh directory: Inno's self-deletion helper may still be cleaning the old uninstaller.
$portableTestDir = Join-Path $env:RUNNER_TEMP 'MonitorSwitch portable ownership test'
Invoke-Installer $SetupPath @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$portableTestDir", "/LOG=$logDir\reinstall.log")
$portableCommand = '"C:\Portable MonitorSwitch\MonitorSwitch.exe"'
Set-ItemProperty -LiteralPath $runKey -Name MonitorSwitch -Value $portableCommand -Type String
try {
    Invoke-Installer (Join-Path $portableTestDir 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$logDir\portable-uninstall.log")
    if ((Get-ItemPropertyValue -LiteralPath $runKey -Name MonitorSwitch) -ne $portableCommand) {
        throw 'Uninstall removed the startup preference of a portable copy.'
    }
}
finally {
    Remove-ItemProperty -LiteralPath $runKey -Name MonitorSwitch -ErrorAction SilentlyContinue
}
Write-Host 'Install, upgrade, WinUI startup, uninstall, and startup preference cleanup passed.'
