$ErrorActionPreference = 'Stop'
$TestRoot = $PSScriptRoot
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $TestRoot '..'))
$BuildRoot = Join-Path $TestRoot 'build'
$VsWhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $VsWhere -PathType Leaf)) {
    throw "Visual Studio locator not found: $VsWhere"
}
$VsRoot = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VsRoot) {
    throw 'Visual Studio C/C++ build tools were not found.'
}
$DevCmd = Join-Path $VsRoot 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$Exe = Join-Path $BuildRoot 'protocol_host_test.exe'
$TestSource = Join-Path $TestRoot 'protocol_host_test.c'
$ProtocolSource = Join-Path $ProjectRoot 'src\protocol.c'
$MockInclude = Join-Path $TestRoot 'mocks'
$ProjectInclude = Join-Path $ProjectRoot 'include'

$Command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c11 /W4 /WX /I"{1}" /I"{2}" "{3}" "{4}" /Fe:"{5}" && "{5}"' -f `
    $DevCmd, $MockInclude, $ProjectInclude, $TestSource, $ProtocolSource, $Exe
cmd.exe /d /s /c $Command
if ($LASTEXITCODE -ne 0) {
    throw "Protocol host tests failed with exit code $LASTEXITCODE."
}

$KeyboardExe = Join-Path $BuildRoot 'keyboard_host_test.exe'
$KeyboardTest = Join-Path $TestRoot 'keyboard_host_test.c'
$KeyboardSource = Join-Path $ProjectRoot 'src\text_keyboard.c'
$KeyboardCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c11 /W4 /WX /I"{1}" "{2}" "{3}" /Fe:"{4}" && "{4}"' -f `
    $DevCmd, $ProjectInclude, $KeyboardTest, $KeyboardSource, $KeyboardExe
cmd.exe /d /s /c $KeyboardCommand
if ($LASTEXITCODE -ne 0) {
    throw "Keyboard host tests failed with exit code $LASTEXITCODE."
}
