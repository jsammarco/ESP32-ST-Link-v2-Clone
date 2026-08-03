param(
    [ValidateSet('all', 'poc', 'minimal')]
    [string]$Target = 'all',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$BuildRoot = Join-Path $ProjectRoot 'build'

if ($env:VAPORWARE) {
    $VaporwareRoot = [IO.Path]::GetFullPath($env:VAPORWARE)
} else {
    $VaporwareRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot '..\..\..\Vaporware\src'))
}

if ($env:ARM_GCC_BIN) {
    $ToolRoot = [IO.Path]::GetFullPath($env:ARM_GCC_BIN)
} else {
    $ToolRoot = 'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin'
}

$Gcc = Join-Path $ToolRoot 'arm-none-eabi-gcc.exe'
$Objcopy = Join-Path $ToolRoot 'arm-none-eabi-objcopy.exe'
$Size = Join-Path $ToolRoot 'arm-none-eabi-size.exe'
$LinkerScript = Join-Path $VaporwareRoot 'n32g031.ld'

foreach ($Required in @(
    $Gcc,
    $Objcopy,
    $Size,
    $LinkerScript,
    (Join-Path $VaporwareRoot 'src\startup.s'),
    (Join-Path $VaporwareRoot 'src\system.c'),
    (Join-Path $VaporwareRoot 'src\display.c'),
    (Join-Path $VaporwareRoot 'include\n32g031.h')
)) {
    if (-not (Test-Path -LiteralPath $Required -PathType Leaf)) {
        throw "Required tool or SDK file not found: $Required"
    }
}

if ($Clean -and (Test-Path -LiteralPath $BuildRoot)) {
    $ResolvedBuild = [IO.Path]::GetFullPath($BuildRoot)
    if (-not $ResolvedBuild.StartsWith([IO.Path]::GetFullPath($ProjectRoot), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean unexpected path: $ResolvedBuild"
    }
    Remove-Item -LiteralPath $ResolvedBuild -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null

function Invoke-Tool {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $File $($Arguments -join ' ')"
    }
}

$Cpu = @('-mcpu=cortex-m0', '-mthumb')
$Includes = @(
    "-I$ProjectRoot\include",
    "-I$VaporwareRoot\include"
)
$VendorCFlags = $Cpu + $Includes + @(
    '-Os',
    '-std=c11',
    '-ffunction-sections',
    '-fdata-sections',
    '-fno-common',
    '-Wall',
    '-Wextra'
)
$CFlags = $VendorCFlags + @(
    '-Werror',
    '-Wconversion',
    '-Wshadow'
)

$CommonSources = [ordered]@{
    'system' = Join-Path $VaporwareRoot 'src\system.c'
    'display' = Join-Path $VaporwareRoot 'src\display.c'
    'hardware' = Join-Path $ProjectRoot 'src\hardware.c'
    'swd_runtime' = Join-Path $ProjectRoot 'src\swd_runtime.c'
    'runtime_uart' = Join-Path $ProjectRoot 'src\runtime_uart.c'
    'button_gestures' = Join-Path $ProjectRoot 'src\button_gestures.c'
    'text_keyboard' = Join-Path $ProjectRoot 'src\text_keyboard.c'
    'protocol' = Join-Path $ProjectRoot 'src\protocol.c'
    'display_ui' = Join-Path $ProjectRoot 'src\display_ui.c'
    'menu_app' = Join-Path $ProjectRoot 'src\menu_app.c'
}

$StartupObject = Join-Path $BuildRoot 'startup.o'
Invoke-Tool $Gcc ($Cpu + @(
    '-x', 'assembler-with-cpp', '-c',
    (Join-Path $VaporwareRoot 'src\startup.s'),
    '-o', $StartupObject
))

$CommonObjects = @()
foreach ($Entry in $CommonSources.GetEnumerator()) {
    $Object = Join-Path $BuildRoot ($Entry.Key + '.o')
    $Flags = if ($Entry.Key -in @('system', 'display')) { $VendorCFlags } else { $CFlags }
    Invoke-Tool $Gcc ($Flags + @('-c', $Entry.Value, '-o', $Object))
    $CommonObjects += $Object
}

function Build-Image {
    param([string]$Name, [string]$MainSource)

    $MainObject = Join-Path $BuildRoot ($Name + '_main.o')
    $Elf = Join-Path $BuildRoot ($Name + '.elf')
    $Map = Join-Path $BuildRoot ($Name + '.map')
    $Bin = Join-Path $BuildRoot ($Name + '.bin')
    $Hex = Join-Path $BuildRoot ($Name + '.hex')

    Invoke-Tool $Gcc ($CFlags + @('-c', $MainSource, '-o', $MainObject))
    Invoke-Tool $Gcc ($Cpu + @(
        "-T$LinkerScript",
        '-Wl,--gc-sections',
        "-Wl,-Map=$Map",
        '-Wl,--print-memory-usage',
        '-nostdlib',
        '-lnosys',
        $StartupObject
    ) + $CommonObjects + @($MainObject, '-o', $Elf))
    Invoke-Tool $Objcopy @('-O', 'binary', $Elf, $Bin)
    Invoke-Tool $Objcopy @('-O', 'ihex', $Elf, $Hex)
    Invoke-Tool $Size @($Elf)
    Write-Host "Built $Bin"
}

if (($Target -eq 'all') -or ($Target -eq 'poc')) {
    Build-Image 'raz_esp32_poc' (Join-Path $ProjectRoot 'src\main.c')
}
if (($Target -eq 'all') -or ($Target -eq 'minimal')) {
    Build-Image 'raz_minimal_test' (Join-Path $ProjectRoot 'src\minimal_main.c')
}
