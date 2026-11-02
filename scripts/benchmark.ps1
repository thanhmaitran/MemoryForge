<#
.SYNOPSIS  Build a Release binary, run every benchmark config and generate charts.
.DESCRIPTION  Results go to reports\local\ (git-ignored). Copy a verified run into
              reports\ yourself, together with its JSON metadata.
.EXAMPLE   .\scripts\benchmark.ps1 -Repetitions 7
#>
param(
    [int]$Repetitions = 5,
    [string]$Preset = "vs2022"
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

& "$PSScriptRoot\build.ps1" -Config Release -Preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Get-ChildItem -Path "build\$Preset" -Recurse -Filter "memoryforge.exe" |
    Where-Object { $_.FullName -match "Release" } | Select-Object -First 1
if (-not $exe) { throw "Release memoryforge.exe not found" }

python benchmarks\scripts\run_all.py --exe $exe.FullName --out reports\local --repetitions $Repetitions
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
python analysis\plot_results.py --reports reports\local --out reports\local\charts
exit $LASTEXITCODE
