# Laboratory validation — 2026-09-05

## Result

The independent headless server was built and exercised with eight fresh strict bots (four per faction, including a hunter). The final binary sustained **3.7006×** at a requested 4×, and **0.99994×** after a graceful restart at 1×. No non-strict characters were present. The play installation was not rebuilt, started, stopped, or reconfigured during this sandbox task.

Final executable SHA-256:

`35b3cd6b1bc08e8b75c8c0c3ed61a2b969e8882561463fd0b778e2e6af07606d`

## Final runtime evidence

Both runs used that same binary and the same continuing roster. They are smoke checks, not an A/B behavior experiment.

| Check | Accelerated run | Restart run |
| --- | ---: | ---: |
| Run ID | `20260905-123031-9bedab` | `20260905-123245-d6e6c5` |
| Requested / achieved speed | 4 / 3.7006 | 1 / 0.99994 |
| Clock samples | 9 | 18 |
| Complete minute probes | 5 | 3 |
| Online bots in every probe | 8 | 8 |
| XP at last minute probe | 4,780 | 2,689 |
| Kills at last minute probe | 91 | 44 |
| Deaths at last minute probe | 0 | 0 |
| Saved played seconds per bot after shutdown | 1,059 | 1,237 |

Every adjacent probe advanced the monotonic clock by exactly 60,000 ms, the gameplay and Unix clocks by 60 seconds, and aggregate character played time by 480 seconds. Gameplay and Unix clocks agreed in every probe. The fixed world step remained 50 ms throughout; speed is calculated from simulation-time differences divided by measured wall-time differences, excluding the initial clock sample.

Durable character saves confirmed movement and progression. All eight characters finished at level 3. Earlier preliminary checks took them from level 1 to levels 2–3; the hunter also performed a normal ammunition purchase. Neither those preliminary counts nor startup fragments are pooled into the final table. Event counters above end at the last periodic probe, while character endpoints include the remaining shutdown tail.

The 4× run checkpointed epoch `1788601011258772200` ns. The following run started at `1788601012000000000` ns and ended at `1788601205897142400` ns. Its actual first gameplay probe was also later than the checkpoint. This verifies restart with an accelerated future date without negative offline duration.

Raw logs, character snapshots, hashes, and machine-readable results are outside Git:

- `D:\wowbot-lab\validation\final-4x\`
- `D:\wowbot-lab\validation\final-1x-restart\`
- `D:\wowbot-lab\validation\restart.json`
- `D:\wowbot-lab\source-manifest.json` (3,568 source-file hashes, including copied local dependency changes)

## Isolation and other checks

- All 26 protected file hashes matched after testing, including `StartServer.bat`, `WorldserverLoop.bat`, normal server executables/configuration, `Wow.exe`, and the client realmlist.
- The observed laboratory database listened on `127.0.0.1:13316` and reported the private datadir. The original MySQL process on port 3306 remained present.
- The world process was named `wowbot-lab-world.exe`; no laboratory process used the normal launcher's `worldserver.exe`/`authserver.exe` names.
- The documented `StartLab.ps1 -Speed 4` also cold-started the lab with its private database stopped (`20260905-124056-dc5ad1`). The database, world, and all eight strict bots came up successfully, with continued XP/kill activity. This additional lifecycle check is not pooled into the table above.
- Final roster check: eight characters, eight registered/active strict bots, zero non-strict characters, and zero online after graceful shutdown.
- Full isolated `worldserver` build passed with zero errors.
- Both C++ clock tests passed, including startup with a future epoch and cross-thread/cross-translation-unit consistency.
- All seven Python isolation/process-identity tests passed.
- `git diff --check` passed. The whole copied core's style check was not clean (for example, existing tabs in `DBCStructure.h`); unrelated source formatting was not rewritten.

The first empty-schema startup exposed a required `active_arena_season` seed row. The preparation recipe now includes the core's fresh-install `(8,1)` value. That failed startup had no test population and is excluded from runtime results.

## Limits

This establishes a usable accelerated engineering sandbox and a working clean restart. It does not prove behavior equivalence across every quest, long-running calendar event, dungeon, profession, auction, or PvP system. Speed 20 is an input limit, not a measured throughput guarantee. OS/database/network waits and SQL-generated timestamps remain real-time; use simulation/played duration for exposure. Validate future behavior changes at real time as well. The source snapshot is independent, so future module changes must be deliberately brought into it.
