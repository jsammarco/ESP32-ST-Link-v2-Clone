$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
& python (Join-Path $repoRoot 'tools\raz_manager_gui.py')
exit $LASTEXITCODE
