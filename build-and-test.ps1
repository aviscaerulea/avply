# vim: set ft=ps1 fenc=utf-8 ff=unix sw=4 ts=4 et :
# avply ビルド + ユニットテスト実行
# AVPLY_BUILD_TESTS=ON で構成し、cmake --build → ctest を一気通貫で実行する。
# build.ps1 は無改変のまま並走する（こちらは tests/ も含めるため別経路）
#
# 並列禁止：test_Settings が HKCU\Software\avply\avply の値を退避→初期化→復元する設計のため、
# 複数テストを同時実行すると同一レジストリキーへの競合が発生する。
# ctest は既定で逐次実行（-j 未指定）のため明示しない
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

# AVPLY_BUILD_TESTS=ON のキャッシュが入っていなければ再 configure する
$cache = Join-Path $buildDir "CMakeCache.txt"
$needsReconfigure = -not (Test-Path $cache) -or
    -not (Select-String -Path $cache -Pattern 'AVPLY_BUILD_TESTS:BOOL=ON' -Quiet)
if ($needsReconfigure) {
    cmake --preset msvc-release -DAVPLY_BUILD_TESTS=ON
    if ($LASTEXITCODE) { exit 1 }
}

cmake --build --preset msvc-release
if ($LASTEXITCODE) { exit 1 }

# ヘッドレス実行向けに QPA を offscreen に切り替えてからテスト実行
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir $buildDir -C Release --output-on-failure
exit $LASTEXITCODE
