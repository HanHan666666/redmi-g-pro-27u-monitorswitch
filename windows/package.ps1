#Requires -Version 7.0
<#
.SYNOPSIS
    Package the Release x64 build as a portable ZIP and per-user EXE installer.
#>
param(
    [ValidatePattern('^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$')]
    [string]$Version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION.txt') -Raw).Trim(),
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $PSScriptRoot 'build\Release'
$outputDir = Join-Path $PSScriptRoot 'dist'
$stageDir = Join-Path $PSScriptRoot ('build\packages\payload-' + [guid]::NewGuid().ToString('N'))
$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$programFiles = [Environment]::GetFolderPath('ProgramFiles')
$localPrograms = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs'

if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'MonitorSwitch.exe'))) {
    throw 'Build Release x64 with windows/build.ps1 before packaging.'
}

if (-not $IsccPath) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compiler) { $IsccPath = $compiler.Source }
    foreach ($candidate in @(
        "$programFilesX86\Inno Setup 6\ISCC.exe",
        "$programFiles\Inno Setup 7\ISCC.exe",
        "$localPrograms\Inno Setup 6\ISCC.exe",
        "$localPrograms\Inno Setup 7\ISCC.exe"
    )) {
        if (-not $IsccPath -and (Test-Path -LiteralPath $candidate)) { $IsccPath = $candidate }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) {
    throw 'Install Inno Setup 6.3+ or pass -IsccPath with the full path to ISCC.exe.'
}

# App-local CRT deployment keeps installation per-user and offline.
$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio vswhere.exe was not found.' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsPath) { throw 'The Visual C++ x64 build tools were not found.' }
$redistRoot = Join-Path $vsPath 'VC\Redist\MSVC'
$crtDir = Get-ChildItem -LiteralPath $redistRoot -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $crtDir) { throw 'The Visual C++ x64 redistributable DLLs were not found.' }

New-Item -ItemType Directory -Path $stageDir, $outputDir -Force | Out-Null
$excludedExtensions = @('.pdb', '.ilk', '.lib', '.exp', '.obj', '.iobj', '.ipdb', '.log')
foreach ($file in Get-ChildItem -LiteralPath $buildDir -Recurse -File) {
    if ($file.Extension -in $excludedExtensions) { continue }
    $relative = [IO.Path]::GetRelativePath($buildDir, $file.FullName)
    $destination = Join-Path $stageDir $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}
Get-ChildItem -LiteralPath $crtDir -Filter '*.dll' -File |
    Copy-Item -Destination $stageDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $stageDir
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $stageDir 'README-Windows.md')

foreach ($required in @(
    'MonitorSwitch.exe', 'Microsoft.ui.xaml.dll', 'Microsoft.UI.Xaml.Controls.pri',
    'MtkDirectTool.jar', 'VolDirectTool.jar', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageDir $required))) {
        throw "The package is missing $required."
    }
}

$zipPath = Join-Path $outputDir "MonitorSwitch-$Version-Windows-x64.zip"
# Compress-Archive skips hidden files, so use ZipFile to preserve every runtime resource.
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath }
[IO.Compression.ZipFile]::CreateFromDirectory($stageDir, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $false)

$compilerArgs = @(
    "/DAppVersion=$Version", "/DSourceDir=$stageDir", "/DOutputDir=$outputDir",
    (Join-Path $PSScriptRoot 'installer\MonitorSwitch.iss')
)
& $IsccPath @compilerArgs
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }

$setupPath = Join-Path $outputDir "MonitorSwitch-$Version-Windows-x64-Setup.exe"
if (-not (Test-Path -LiteralPath $setupPath)) { throw 'The installer was not created.' }
$hashLines = foreach ($path in @($setupPath, $zipPath)) {
    $hash = Get-FileHash -LiteralPath $path -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), [IO.Path]::GetFileName($path)
}
$hashLines | Set-Content -LiteralPath (Join-Path $outputDir "MonitorSwitch-$Version-Windows-x64-SHA256SUMS.txt") -Encoding utf8
Get-Item -LiteralPath $setupPath, $zipPath | Select-Object Name, Length
