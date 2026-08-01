[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    # Probe validates the local ESP32 SWD implementation and writes nothing.
    [switch]$ProbeOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$adapterDir = Split-Path -Parent $PSScriptRoot
$repoDir = Split-Path -Parent $adapterDir
$launcherDir = Join-Path $repoDir 'examples\Launcher'
$fastFlasher = Join-Path $PSScriptRoot 'fast_flash.py'

if ($ProbeOnly) {
    & python $fastFlasher --port $Port --probe
    exit $LASTEXITCODE
}

$image = Join-Path $launcherDir 'build\launcher.bin'
if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
    throw "Launcher binary is missing. Run '$launcherDir\build_launcher.bat' first."
}

& python $fastFlasher --port $Port --flash $image
exit $LASTEXITCODE
