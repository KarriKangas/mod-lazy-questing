# Accelerated headless bot laboratory

The laboratory lives at `D:\wowbot-lab`. It has an independent source snapshot, build, executable, map data, configuration, logs, MySQL instance, and databases. The normal installation at `D:\wowserver`, its launchers, and the WoW client are not used as runtime write targets.

## Start, inspect, and stop

From this repository:

```powershell
.\sandbox\StartLab.ps1 -Speed 4
python sandbox/lab.py status
python sandbox/lab.py command 'strictbots aquarium roster'
.\sandbox\StopLab.ps1
```

Startup is asynchronous. `status` reports the actual process state and the per-run console log. Read the `[SANDBOX] speed=...` line and subsequent tick reports to confirm world readiness. Commands are queued to the private process's console; `.sent` means delivered, not necessarily successfully executed. Inspect console output for the result.

`StopLab.ps1` requests a graceful world shutdown. Wait for `status` to show the world stopped. To release the private database's resources too:

```powershell
python sandbox/lab.py stop-db
```

Starting the lab again restarts its private database if needed. The world runs at below-normal process priority. No Windows service or recurring task is installed.

## Isolation

| Resource | Laboratory |
| --- | --- |
| World process | `wowbot-lab-world.exe` |
| World listener | `127.0.0.1:18085` |
| MySQL | private datadir, `127.0.0.1:13316` |
| Databases | `lab_auth`, `lab_characters`, `lab_world`, `lab_playerbots` |
| Authentication server | none; strict bots use internal sessions |
| SOAP / remote access | disabled |
| Population | fresh strict test bots only; random and add-class account pools disabled |

The distinct executable name matters: the normal `StartServer.bat` checks for any process called `worldserver.exe`. The laboratory never starts a process with that name or changes the client's realmlist. It is a headless test environment; connecting an ordinary client to accelerated simulation is outside its contract.

The launcher validates database names, connection endpoints, paths, and population settings before startup, verifies the MySQL datadir, and strips inherited `AC_` configuration overrides from the child environment. It never kills processes by name. Private credentials and copied database contents remain outside Git, under a directory ACL restricted to the current user and SYSTEM.

## What acceleration means

Every world step advances **50 milliseconds of simulation time**. Speed changes only the wall-time pacing of those steps. Gameplay clocks used by the core, event scheduling, spells, and Playerbots are redirected in the private source snapshot to the same monotonic simulation clock. Direct Unix-time queries in gameplay use a matching simulated epoch clock.

The clock stays fixed during a world step; all workers see the same simulated time. If the CPU cannot meet the requested pace, achieved speed drops instead of increasing the step size. `[SANDBOX]` telemetry reports requested speed, achieved speed, simulation milliseconds, real elapsed milliseconds, and tick count.

Graceful shutdown checkpoints the simulated date. Startup uses the latest of that checkpoint, the real date, and the latest character save, before gameplay initialization. This prevents accelerated timestamps from moving backwards on restart. Stopped time is not accelerated. Preserve evidence after a crash; crash recovery is not equivalent to a clean experiment restart.

XP, movement speeds per simulation second, damage, loot, cooldown durations, and other gameplay rules are not multiplied. Global bot cheats and automatic level-based teleportation are disabled. Network I/O, database operations, process supervision, profiling, and wall-time pacing remain on real time. Calendar events in gameplay advance with simulation time; SQL-generated timestamps remain wall time. Analyze played/simulation duration, not SQL date differences, for accelerated exposure.

Supported speed inputs are 1–20; this is an input bound, not a claim that every machine or population sustains 20×. Start at 4× and consult achieved-speed telemetry. Accelerated runs are engineering tests; new behavior should still receive a real-time validation before promotion. A short smoke test does not establish equivalence for every quest, dungeon, or PvP mechanic.

## Fresh population

To add a new small cohort in an empty lab:

```powershell
python sandbox/lab.py command 'strictbots guild alliance "Lab Alliance" 4'
python sandbox/lab.py command 'strictbots guild horde "Lab Horde" 4'
```

The checked-in controller remains an inert skeleton. These bots exercise existing strict Playerbots behavior. A sandbox-only observer records XP, kills, deaths, online count, played time, and clock agreement. It grants nothing and requests no gameplay action.

## Rebuild and reproduce

```powershell
.\sandbox\BuildLab.ps1
```

This builds only the isolated tree. Source changes in the main project do **not** automatically propagate into the snapshot. Keep deliberate sandbox changes reproducible in the preparation overlay; do not point its build at the play installation.

For first-time preparation on this machine, with `D:\wowbot-lab` absent:

```powershell
python sandbox/prepare.py
python sandbox/setup_runtime.py
.\sandbox\BuildLab.ps1
```

Preparation reads the local server configuration privately to perform non-locking database dumps. It copies world content and static bot caches, uses fresh account/character schemas, initializes required fresh-install state, and removes copied bot identities. It never imports old characters or accounts. The scripts reject overwriting an unmarked directory or reapplying the source overlay to a completed snapshot. They require the local CMake, VS 2026, Python, MySQL 8.4, Boost, and OpenSSL paths shown in `BuildLab.ps1` and `setup_runtime.py`.

`source-manifest.json` records source revisions and overlay hashes; `live-baseline.json` records protected play-installation hashes. Raw logs stay outside Git. Each graceful run preserves its server/playerbot/error logs with its run ID. A forced process loss may leave only the current logs: preserve those before another startup.

## Checks

Run `verify.py` while the private database is running (starting the lab starts that database). It deliberately does not start services as a side effect of verification.

```powershell
python sandbox/verify.py
python -m unittest discover -s sandbox/tests -p 'test_*.py'
cmake -S sandbox/tests -B D:\wowbot-lab\clock-tests -G "Visual Studio 18 2026" -A x64
cmake --build D:\wowbot-lab\clock-tests --config RelWithDebInfo
ctest --test-dir D:\wowbot-lab\clock-tests -C RelWithDebInfo --output-on-failure
```

The clock tests cover fixed-step accumulation, paused wall time, Unix/steady/system agreement, shared state across translation units, and concurrent monotonic reads. Isolation tests exercise rejection of normal-server database connections, escaping data paths, public binding, extra account pools, and cheats. `verify.py` checks current protected-file hashes, actual database identity, and the roster; its printed population counts must also be inspected.

See `VALIDATION.md` for the completed runtime checks and their limits.
