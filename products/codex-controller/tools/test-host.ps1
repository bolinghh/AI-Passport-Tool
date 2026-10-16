$ErrorActionPreference = 'Stop'
$product = Split-Path $PSScriptRoot -Parent
$build = Join-Path $product 'build-host'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Install Visual Studio C++ Build Tools, or run the CMake host test with a C compiler.' }
    $setup = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    & cmd.exe /d /c "call `"$setup`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
    }
}
Push-Location $build
try {
    & cl.exe /nologo /std:c11 /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS "/I$product/main" "$product/tests/test_controller.c" "$product/main/controller.c" "$product/main/wire.c" /Fe:test_controller.exe
    if ($LASTEXITCODE) { throw 'Host compile failed' }
    & ./test_controller.exe "$product/tests/wire-vectors.txt"
    if ($LASTEXITCODE) { throw 'Host tests failed' }
} finally { Pop-Location }
