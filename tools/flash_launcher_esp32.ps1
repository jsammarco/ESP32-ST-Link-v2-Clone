[CmdletBinding()]
param(
    # Perform only a read-only SWD connection / register test.
    [switch]$ProbeOnly,

    # Override only when OpenOCD was deliberately installed elsewhere.
    [string]$OpenOcdPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$adapterDir = Split-Path -Parent $PSScriptRoot
$repoDir = Split-Path -Parent $adapterDir
$launcherDir = Join-Path $repoDir 'examples\Launcher'
$configPath = Join-Path $adapterDir 'n32g031-esp32.openocd.cfg'

if ([string]::IsNullOrWhiteSpace($OpenOcdPath)) {
    $OpenOcdPath = Join-Path $PSScriptRoot 'openocd-windows\bin\openocd.exe'
}

if (-not (Test-Path -LiteralPath $OpenOcdPath -PathType Leaf)) {
    throw "Windows OpenOCD was not found at '$OpenOcdPath'. Run bash tools/build_openocd_windows.sh in WSL first."
}

$openOcdRoot = Split-Path -Parent (Split-Path -Parent $OpenOcdPath)
$scriptPath = Join-Path $openOcdRoot 'share\openocd\scripts'
if (-not (Test-Path -LiteralPath $scriptPath -PathType Container)) {
    throw "OpenOCD script directory was not found at '$scriptPath'. Rebuild the Windows OpenOCD package."
}

# Check both the local TCP listener and the ESP32's raw protocol before asking
# OpenOCD to touch the target. This is intentionally the same 'c' exchange
# used during initial bridge validation.
$client = [Net.Sockets.TcpClient]::new()
try {
    $connect = $client.ConnectAsync('127.0.0.1', 3335)
    if (-not $connect.Wait(3000)) {
        throw 'Timed out connecting to 127.0.0.1:3335.'
    }

    $stream = $client.GetStream()
    $stream.ReadTimeout = 3000
    $stream.WriteByte([byte][char]'c')
    $reply = $stream.ReadByte()
    if ($reply -notin 48, 49) {
        throw "Unexpected ESP32 response '$reply'."
    }
}
catch {
    throw "The ESP32 serial bridge is not ready. Keep 'python tools\\serial_bridge.py --port COM7' running, then retry. Details: $($_.Exception.Message)"
}
finally {
    $client.Dispose()
}

$probeCommands = @(
    'init',
    'halt',
    'reg',
    'exit'
)

Write-Host 'Checking SWD target connection...'
$probeArgs = @('-s', $scriptPath, '-f', $configPath)
foreach ($command in $probeCommands) {
    $probeArgs += @('-c', $command)
}
& $OpenOcdPath @probeArgs
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD could not communicate with the target. The ESP32 bridge already tried both GPIO25/GPIO26 SWD mappings. Check GND, wake the vape, and keep the CC wires short. Nothing was written."
}

if ($ProbeOnly) {
    Write-Host 'SWD probe passed; no firmware was written.'
    exit 0
}

if (-not (Test-Path -LiteralPath (Join-Path $launcherDir 'build\launcher.bin') -PathType Leaf)) {
    throw "Launcher binary is missing. Run '$launcherDir\\build_launcher.bat' first."
}

Push-Location $launcherDir
try {
    Write-Host 'Generating Launcher flash commands...'
    & python .\gen_direct_flash.py
    if ($LASTEXITCODE -ne 0) {
        throw 'gen_direct_flash.py failed.'
    }

    Write-Host 'Flashing Launcher and verifying each page...'
    & $OpenOcdPath -s $scriptPath -f $configPath `
        -c 'tcl_port disabled; telnet_port disabled; gdb_port disabled' `
        -c 'init' `
        -c 'source direct_flash.tcl' `
        -c 'exit'
    if ($LASTEXITCODE -ne 0) {
        throw 'OpenOCD reported a flash failure.'
    }
}
finally {
    Pop-Location
}

Write-Host 'Launcher flash complete. The vape was reset and the Launcher started.'
