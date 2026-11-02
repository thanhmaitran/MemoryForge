<#
.SYNOPSIS  Configure and build MemoryForge on Windows (Visual Studio Build Tools / MSVC).
.EXAMPLE   .\scripts\build.ps1 -Config Release
#>
param(
    [ValidateSet("Debug", "Release")] [string]$Config = "Release",
    [string]$Preset = "vs2022"
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset "$Preset-$($Config.ToLower())"
exit $LASTEXITCODE
