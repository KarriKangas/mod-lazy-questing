# Cohort 12: combat-tempo assist vs control

## Verdict

Aborted and invalid by design: the treatment used an outgoing PvE damage multiplier, which
violated the project's non-cheating requirement. The server was stopped immediately after the
user identified the violation. This startup fragment is excluded from all efficacy analysis, its
roster will not be reused, and the damage mechanism has been removed from source and configuration.

## Run metadata

| Field | Value |
| --- | --- |
| Date | 2026-08-31 |
| Cohort | Cohort 12 (invalid/aborted) |
| Run ID | `cohort-12-combat-tempo-assist-vs-control` |
| Population | 80 fresh level-1 strict Altbots |
| Guilds | Cohort 12 Horde (44), Cohort 12 Alliance (45) |
| Factions | 40 Alliance, 40 Horde |
| Experiment arms | 40 control, 40 invalid damage-assist |
| Seed | `38671521` |
| Configuration | control 50%, assist-only 50%, current 0%; atomic interactions off; invalid 20% PvE damage multiplier |
| Code/build | invalid binary SHA-256 `4FEADF641F273CDF6A3D5F54284C08916189FD1CA1450F275E567EF4AA8B0DEE` |
| Observation | One partial startup interval; far short of the 25,200-second endpoint |
| Recorder epochs | One process start at `2026-08-31 13:38:10 +03:00`, PID 20900 |
| Exclusions | Entire run excluded; PID 20900 was forcibly stopped before further exposure |

The locked roster SHA-256 was
`7f7764f77ba53ec418b8edf080f7b5fd5e0dac85c4d17afe37861f126531e531`.
The prelaunch roster was balanced 40/40, 20/20 per faction, optimally within every race/class,
and within 0.34% on all starting equipment means. That design work does not make the treatment
valid.

## Hypothesis and treatment contract

The proposed multiplier was intended to test whether combat duration constrained progression,
but it directly changed combat power and therefore violated the governing contract: strict
Altbots must level through legal game behavior without cheats, synthetic stat bonuses, direct XP,
or manipulated loot/equipment.

Treatment may:

- nothing from this invalid implementation.

Treatment may not:

- modify damage, stats, XP, loot, item attributes, or other game mechanics;
- be promoted, analyzed as efficacy evidence, or reused in a clean cohort.

Primary promotion gates were never applicable because the treatment contract failed before the
fixed exposure began.

## Assignment and exposure integrity

Assignment was balanced, but exposure integrity failed by definition because the entire treatment
was out of scope and the server was stopped after one interval.

| Category | Total | Control | Invalid treatment |
| --- | ---: | ---: | ---: |
| Alliance | 40 | 20 | 20 |
| Horde | 40 | 20 | 20 |

## Implementation sanity check

The recorder proved the invalid behavior was active: control recorded 0 assisted hits, while the
treatment recorded 122 assisted hits, 1,444 pre-bonus damage, and 287 bonus damage (19.88%). This
is evidence of contamination, not successful activation.

## Flight-recorder results

No progression rates are reported. The sole interval is a startup fragment and the treatment was
invalid.

## Activity mix

Not analyzed.

## Equipment progression

Not analyzed. The roster is contaminated and will be retired rather than reset or reused.

## Database endpoint

Not analyzed; no bot approached the fixed exposure.

## Time and level-band trends

Not analyzed; only one startup interval exists.

## Intent and scheduler health

Assignment reached 40/40 and active Lazy Questing intents remained zero. These facts do not rescue
an invalid intervention.

## Robustness, tails, and caveats

- The server was forcibly terminated to minimize continued invalid exposure; this recorder epoch
  is an intentionally excluded shutdown fragment.
- No efficacy estimate, favorable or unfavorable, may be inferred from 122 assisted hits or one
  recorder interval.
- The safe live configuration was immediately changed to 100% control, 0% assist, and zero damage
  before rebuilding.

## Decision and next experiment

Reject permanently. The damage hook, configuration key, recorder fields, and documentation were
removed, and the server was rebuilt without them. The next cohort must be fresh and test only a
behavioral change available to an ordinary player: better target selection, reduced intent churn,
shorter legal travel, reliable post-kill looting, or better service/equip decisions. It must retain
the same fixed exposure and progression, loot, equal-level gear, travel, stall, and tail gates.
