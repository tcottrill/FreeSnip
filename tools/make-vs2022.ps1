# Generates a Visual Studio 2022 solution, build/vs2022/FreeSnip.sln, with CMake.
# Open it, pick Debug or Release, and build or debug freesnip (the startup project).
# Release writes dist/freesnip.exe, Debug dist/debug/freesnip.exe, as tools/build-windows.ps1.
# The wxWidgets libraries are projects in the "Libraries" and "Third Party Libraries" folders.
#
# Run it again after adding or removing source files (CMakeLists.txt changes regenerate the
# solution on the next build by themselves). -Open opens the solution afterwards.
param([switch]$Open)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = Join-Path $repo 'build/vs2022'
if (!(Test-Path -LiteralPath (Join-Path $repo 'vendor/wxWidgets/CMakeLists.txt'))) {
    throw 'wxWidgets is missing: run git submodule update --init --recursive'
}
cmake -S $repo -B $build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'Generating the solution failed' }
$sln = Join-Path $build 'FreeSnip.sln'
Write-Host "Solution: $sln"
if ($Open) { Invoke-Item -LiteralPath $sln }
