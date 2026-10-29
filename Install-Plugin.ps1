param(
    [string[]]$OBSPluginDirs = @(
        "D:\Program Files\obs-studio\obs-plugins\64bit",
        "D:\Steam\steamapps\common\OBS Studio\obs-plugins\64bit",
        "C:\Program Files\obs-studio\obs-plugins\64bit"
    ),
    [switch]$RestartOBS
)

$ErrorActionPreference = "Continue"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$DistBin = Join-Path $ProjectRoot "dist\obs-vr-mirror-by-u2vr\bin\64bit"
$SourceDll = Join-Path $DistBin "obs-vr-mirror-by-u2vr.dll"
$OpenVRDll = Join-Path $DistBin "openvr_api.dll"

if (-not (Test-Path $SourceDll)) {
    throw "Сначала соберите плагин через .\Build-Plugin.ps1"
}

$obsProc = Get-Process obs64 -ErrorAction SilentlyContinue
$obsPath = $null
$obsDir = $null

if ($obsProc) {
    $obsPath = $obsProc[0].Path
    $obsDir = Split-Path -Parent $obsPath

    if ($RestartOBS) {
        Write-Host "Закрываем OBS Studio для обновления плагина..." -ForegroundColor Yellow
        $obsProc[0].CloseMainWindow() | Out-Null
        $exited = $obsProc[0].WaitForExit(6000)
        if (-not $exited) {
            Stop-Process -Id $obsProc[0].Id -Force
            Start-Sleep -Milliseconds 800
        }
        Start-Sleep -Milliseconds 500
    } else {
        Write-Warning "OBS Studio сейчас запущен. Чтобы обновить заблокированные DLL, запустите .\Install-Plugin.ps1 -RestartOBS"
    }
}

$installedCount = 0
foreach ($dir in $OBSPluginDirs) {
    if (Test-Path $dir) {
        try {
            # Удаляем старый файл obs-vr-stabilizer.dll, если остался
            $oldDll = Join-Path $dir "obs-vr-stabilizer.dll"
            if (Test-Path $oldDll) {
                Remove-Item $oldDll -Force -ErrorAction SilentlyContinue
                Write-Host "[-] Удалена устаревшая версия: $oldDll" -ForegroundColor Gray
            }

            Copy-Item $SourceDll (Join-Path $dir "obs-vr-mirror-by-u2vr.dll") -Force -ErrorAction Stop
            if (-not (Test-Path (Join-Path $dir "openvr_api.dll"))) {
                Copy-Item $OpenVRDll (Join-Path $dir "openvr_api.dll") -Force -ErrorAction SilentlyContinue
            }
            Write-Host "[OK] Плагин установлен в: $dir" -ForegroundColor Green
            $installedCount++
        }
        catch {
            Write-Warning "Не удалось скопировать в $dir : $($_.Exception.Message)"
        }
    }
}

if ($installedCount -eq 0) {
    throw "Не удалось установить плагин ни в одну директорию OBS Studio!"
}

if ($RestartOBS -and $obsPath) {
    Write-Host "Запуск OBS Studio..." -ForegroundColor Yellow
    Remove-Item -Path "$env:APPDATA\obs-studio\.sentinel\*" -Force -ErrorAction SilentlyContinue

    Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine = "`"$obsPath`" --disable-shutdown-check"
        CurrentDirectory = $obsDir
    } | Out-Null
    Write-Host "[OK] OBS Studio успешно запущен с обновленным плагином!" -ForegroundColor Green
} elseif (-not $obsProc) {
    Write-Host "`nOBS Studio не запущен. Плагин загрузится при следующем запуске OBS." -ForegroundColor Cyan
}

Write-Host "`nВ OBS источник доступен в меню '+' как: 'VR Mirror by U2VR'" -ForegroundColor Green
