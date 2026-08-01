[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    # Defaults to a timestamped folder that is ignored by Git.
    [string]$Destination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $Destination = Join-Path $repoRoot "backups\raz-$stamp"
}

Write-Host 'Creating a read-only internal-flash and RAM snapshot...'
& python (Join-Path $PSScriptRoot 'fast_flash.py') --port $Port --backup $Destination
exit $LASTEXITCODE
