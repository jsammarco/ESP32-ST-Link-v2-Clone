$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$entryPoint = Join-Path $PSScriptRoot 'raz_manager_gui.py'
$outputRoot = Join-Path $repoRoot 'dist'

if (-not (Get-Command pyinstaller -ErrorAction SilentlyContinue)) {
    throw 'PyInstaller is not installed. Run: python -m pip install pyinstaller pyserial'
}

& pyinstaller --noconfirm --clean --onefile --console --hide-console hide-early `
    --name 'RAZ-ESP32-Manager' `
    --paths $PSScriptRoot `
    --distpath $outputRoot `
    --workpath (Join-Path $repoRoot 'build\pyinstaller') `
    --specpath (Join-Path $repoRoot 'build\pyinstaller') `
    $entryPoint

if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed with exit code $LASTEXITCODE."
}

Write-Host "Built: $(Join-Path $outputRoot 'RAZ-ESP32-Manager.exe')"
