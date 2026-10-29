$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "Visual Studio with C++ x64 tools not found."
}

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
cmd.exe /c "`"$vcvars`" && cmake -B build -G `"NMake Makefiles`" -DCMAKE_BUILD_TYPE=Release && cmake --build build"
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "`n[OK] Плагин успешно собран:" -ForegroundColor Green
Write-Host "  $ProjectRoot\dist\obs-vr-mirror-by-u2vr\bin\64bit\obs-vr-mirror-by-u2vr.dll"
Write-Host "  $ProjectRoot\dist\obs-vr-mirror-by-u2vr\bin\64bit\openvr_api.dll"
