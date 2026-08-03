$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build\host-tests'
$testExe = Join-Path $buildRoot 'raz_html_renderer_host_test.exe'
$objectRoot = Join-Path $buildRoot 'objects'

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
New-Item -ItemType Directory -Path $objectRoot -Force | Out-Null

$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if ($compiler) {
    & $compiler -std=c++11 -Wall -Wextra -Werror `
        "-I$(Join-Path $repoRoot 'src')" `
        (Join-Path $repoRoot 'src\raz_html_renderer.cpp') `
        (Join-Path $repoRoot 'tests\raz_html_renderer_host_test.cpp') `
        -o $testExe
} else {
    $vsDevCmd = Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\*\*\Common7\Tools\VsDevCmd.bat" `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $vsDevCmd) {
        throw 'A host g++ compiler or Visual Studio C++ Build Tools installation is required.'
    }
    $rendererObject = Join-Path $objectRoot 'raz_html_renderer.obj'
    $testObject = Join-Path $objectRoot 'raz_html_renderer_host_test.obj'
    $compileCommand = ('"{0}" -no_logo && ' +
        'cl /nologo /std:c++17 /W4 /WX /EHsc /D_CRT_SECURE_NO_WARNINGS /I"{1}" /c "{2}" /Fo:"{5}" && ' +
        'cl /nologo /std:c++17 /W4 /WX /EHsc /D_CRT_SECURE_NO_WARNINGS /I"{1}" /c "{3}" /Fo:"{6}" && ' +
        'link /nologo "{5}" "{6}" /OUT:"{4}"') -f `
        $vsDevCmd.FullName, (Join-Path $repoRoot 'src'), `
        (Join-Path $repoRoot 'src\raz_html_renderer.cpp'), `
        (Join-Path $repoRoot 'tests\raz_html_renderer_host_test.cpp'), $testExe, `
        $rendererObject, $testObject
    & cmd.exe /d /c $compileCommand
}
if ($LASTEXITCODE -ne 0) {
    throw "HTML renderer host-test build failed with exit code $LASTEXITCODE."
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "HTML renderer host tests failed with exit code $LASTEXITCODE."
}
