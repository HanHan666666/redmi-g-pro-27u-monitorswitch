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
# Installing a second time must update the same installation successfully.
Invoke-Installer $SetupPath ($installArgs + "/LOG=$logDir\upgrade.log")

foreach ($name in @('MonitorSwitch.exe', 'unins000.exe', 'Microsoft.ui.xaml.dll', 'vcruntime140.dll', 'MtkDirectTool.jar', 'VolDirectTool.jar')) {
    if (-not (Test-Path -LiteralPath (Join-Path $installDir $name))) { throw "Installed payload is missing $name." }
}

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = Join-Path $installDir 'MonitorSwitch.exe'
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.Environment['MONITORSWITCH_NO_AUTO_HIDE'] = '1'
$application = [Diagnostics.Process]::Start($startInfo)
try {
    Start-Sleep -Seconds 8
    if ($application.HasExited) { throw "MonitorSwitch exited during startup: $($application.ExitCode)" }
    $appLog = Join-Path $env:LOCALAPPDATA 'MonitorSwitch\app.log'
    $text = Get-Content -LiteralPath $appLog -Raw -Encoding unicode
    if ($text -notmatch 'panel created' -or $text -match 'UnhandledException|OnLaunched hresult_error|OnLaunched unknown exception') {
        throw 'The installed WinUI panel did not start successfully. See app.log.'
    }
    Copy-Item -LiteralPath $appLog -Destination $logDir
}
finally {
    if (-not $application.HasExited) { $application.Kill($true); $application.WaitForExit() }
}

Invoke-Installer (Join-Path $installDir 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$logDir\uninstall.log")
if (Test-Path -LiteralPath (Join-Path $installDir 'MonitorSwitch.exe')) { throw 'Uninstall left the application executable behind.' }
Write-Host 'Install, upgrade, WinUI startup, and uninstall passed.'
