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

# 再 configure の要否を 3 つの材料で判定する
# 1. キャッシュの不在：未構成のツリー
# 2. 生成完了マーカーの不在：configure がキャッシュ書き出し後に失敗し、生成まで到達しなかったツリー
#    マーカーを見ないと、壊れたツリーへ build を掛けて原因の分かりにくいエラーになる。
#    検出できるのは生成へ一度も到達していないツリーだけだ。生成に成功した後の configure が失敗すると
#    古いマーカーが残って判定を素通りするが、listfile を変更した場合は後続の ZERO_CHECK が
#    cmake を再実行して失敗を表に出す。
#    generate.stamp は Visual Studio ジェネレータが生成の最後に書く。プリセットが同ジェネレータを
#    固定しているため判定に使える。他のジェネレータへ変えるときはマーカーの有無を確認する
# 3. AVPLY_WHISPER_VULKAN=ON の不在：プリセットの cacheVariables を書き換える前の旧キャッシュ
#    本スクリプトはキャッシュがあれば configure ごと省くため、次に -Reconfigure するまで
#    プリセットの変更を取り込まない。この検査が無いと、旧キャッシュを持つ環境は
#    エラーを出さずに CPU 推論のバイナリを作り続ける
#    （プリセット経由の configure が走れば cmake が cacheVariables を既存キャッシュより優先するので
#    値は正される。ZERO_CHECK 由来の再生成は既存キャッシュを使うため値を直さない）
$cache = Join-Path $buildDir "CMakeCache.txt"
$stamp = Join-Path $buildDir "CMakeFiles\generate.stamp"
$needsReconfigure = -not (Test-Path $cache) -or
    -not (Test-Path $stamp) -or
    -not (Select-String -Path $cache -Pattern 'AVPLY_WHISPER_VULKAN:BOOL=ON' -Quiet)
if ($needsReconfigure) {
    cmake --preset msvc-release
    if ($LASTEXITCODE) { exit 1 }
}

cmake --build --preset msvc-release
if ($LASTEXITCODE) { exit 1 }
