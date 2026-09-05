$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'lab.py') stop
if ($LASTEXITCODE -ne 0) { throw 'Sandbox shutdown request failed' }
