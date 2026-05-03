# vim: set ft=ps1 fenc=utf-8 ff=unix sw=4 ts=4 et :
# avply ビルドスクリプト
# DevShell モジュール経由で VS 開発環境をロードし cmake でビルドする。
param([switch]$Reconfigure)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Write-Error "vswhere.exe が見つからない: $vswhere"; exit 1 }
$vsPath = & $vswhere -products '*' -latest -property installationPath
if (-not $vsPath) { Write-Error "Visual Studio / Build Tools が見つからない"; exit 1 }

$devShellDll = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
if (-not (Test-Path $devShellDll)) { Write-Error "DevShell.dll が見つからない: $devShellDll"; exit 1 }
Import-Module $devShellDll
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64"

$buildDir = Join-Path $PSScriptRoot "out"
if ($Reconfigure -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }
if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    cmake --preset msvc-release
    if ($LASTEXITCODE) { exit 1 }
}

cmake --build --preset msvc-release
if ($LASTEXITCODE) { exit 1 }
