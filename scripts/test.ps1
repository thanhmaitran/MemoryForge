<#
.SYNOPSIS  Build (if needed) and run the whole test suite through CTest.
.EXAMPLE   .\scripts\test.ps1 -Config Debug
#>
param(
    [ValidateSet("Debug", "Release")] [string]$Config = "Debug",
    [string]$Preset = "vs2022"
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

& "$PSScriptRoot\build.ps1" -Config $Config -Preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --preset "$Preset-$($Config.ToLower())"
exit $LASTEXITCODE
