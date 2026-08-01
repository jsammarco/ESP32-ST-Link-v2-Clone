[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [string]$Backup,

    # Explicit acknowledgement: this operation erases all 64 KB of internal flash.
    [switch]$Confirm
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $Confirm) {
    throw 'Restore is destructive. Re-run with -Confirm after verifying the selected backup path.'
}

& python (Join-Path $PSScriptRoot 'fast_flash.py') `
    --port $Port `
    --restore $Backup `
    --confirm-restore
exit $LASTEXITCODE
