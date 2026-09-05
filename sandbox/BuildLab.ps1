$ErrorActionPreference = 'Stop'
$labRoot = 'D:\wowbot-lab'
if (!(Test-Path -LiteralPath "$labRoot\source-manifest.json")) { throw 'Prepare sandbox source first' }
cmake -S "$labRoot\source" -B "$labRoot\build" -G 'Visual Studio 18 2026' -A x64 -DAPPS_BUILD=world-only -DTOOLS_BUILD=none -DSCRIPTS=static -DMODULES=static -DBOOST_ROOT=C:/local/boost_1_91_0 '-DOPENSSL_ROOT_DIR=C:/Program Files/OpenSSL-Win64' -DCMAKE_INSTALL_PREFIX=D:/wowbot-lab/install
if ($LASTEXITCODE -ne 0) { throw 'Sandbox configuration failed' }
cmake --build "$labRoot\build" --config RelWithDebInfo --target worldserver -- /m:4 /nologo /verbosity:minimal "/flp:logfile=$labRoot\logs\build.log;verbosity=normal"
if ($LASTEXITCODE -ne 0) { throw 'Sandbox build failed' }
