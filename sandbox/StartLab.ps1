param([ValidateRange(1,20)][double]$Speed = 4)
$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'lab.py') start --speed $Speed
if ($LASTEXITCODE -ne 0) { throw 'Sandbox launch failed' }
