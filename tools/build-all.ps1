# The complete build, from a Windows checkout: the Windows program and its installer, then
# the Linux AppImage and Flatpak through WSL, then dist/release/ for a GitHub release.
#
# Needs Visual Studio 2022 with the C++ tools, Inno Setup 6 (tools/build-installer.ps1) and,
# for Linux, a WSL distribution with bash, curl, bubblewrap and flatpak with the
# org.freedesktop.Sdk//24.08 SDK (see docs/linux-build.md). -SkipLinux builds only Windows.
param([switch]$SkipLinux, [string]$Distro = 'Ubuntu', [int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
function Step($Name) { Write-Host "`n== $Name" -ForegroundColor Cyan }

Step 'Windows program'
& (Join-Path $PSScriptRoot 'build-windows.ps1') -Jobs $Jobs
Step 'Windows installer'
& (Join-Path $PSScriptRoot 'build-installer.ps1')

if (!$SkipLinux) {
    $root = (& wsl.exe -d $Distro -e wslpath -a ($repo -replace '\\', '/')).Trim()
    if ($LASTEXITCODE -or !$root) { throw "WSL distribution '$Distro' is not available" }
    Step 'Linux AppImage'
    & wsl.exe -d $Distro -e bash "$root/tools/build-linux-appimage.sh"
    if ($LASTEXITCODE) { throw 'AppImage build failed' }
    Step 'Linux program for the Flatpak (Freedesktop 24.08 SDK)'
    # The build tree stays on the Linux side (much faster than /mnt/c).
    & wsl.exe -d $Distro -e bash -c "flatpak run '--filesystem=$root' --filesystem=home --socket=wayland --env=SNIP_JOBS=$Jobs --command=sh org.freedesktop.Sdk//24.08 -c 'cd ""$root"" && SNIP_BUILD_DIR=`$HOME/snip-build sh tools/build-linux.sh'"
    if ($LASTEXITCODE) { throw 'Linux build failed' }
    Step 'Flatpak'
    & wsl.exe -d $Distro -e bash "$root/tools/package-flatpak.sh"
    if ($LASTEXITCODE) { throw 'Flatpak packaging failed' }
}

Step 'Release files'
& (Join-Path $PSScriptRoot 'collect-release.ps1')
