# Cohort 11: atomic in-range assist vs control

## Verdict

Reject the atomic in-range assist treatment: it did not activate enough to be a meaningful
intervention and it missed the progression, late-window, equal-level gear, stall, and tail gates.
At the latest well-sampled common milestone, level 9, the intent-to-treat Kaplan-Meier median was
20,366 played seconds for assist-only versus 21,170 for control, a 3.80% improvement rather than
the required 5%. The exact retained second epoch had 0.50% lower XP per bot-hour but substantially
more loot; however, only two atomic interactions succeeded in the entire observed run, both near
launch, so the arm differences cannot credibly be attributed to the treatment. Diagnose the
activation ceiling before changing progression behavior or running another promotion cohort.

## Run metadata

| Field | Value |
| --- | --- |
| Date | 2026-08-30 to 2026-08-31 |
| Cohort | Cohort 11 |
| Run ID | `cohort-11-atomic-in-range-assist-vs-control` |
| Population | 80 fresh level-1 strict Altbots; fixed endpoint 25,200 played seconds since first login |
| Guilds | Cohort 11 Alliance (42), Cohort 11 Horde (43) |
| Factions | 40 Alliance, 40 Horde |
| Experiment arms | 40 control, 40 atomic assist-only |
| Seed | `1032831481`; Cohort 10 seed `54529878` excluded |
| Configuration | control 50%, assist-only 50%, current 0% |
| Code/build | atomic assist in `src/LazyQuestingModule.cpp`; worldserver SHA-256 `7EA2E642E4489308DB7009E17AA660586C36624398AC28A73D36D610DD14F09D` |
| Observation | All 80 reached 25,200 seconds; primary milestones right-censored there. Endpoint collection occurred after scheduler overshoot at 28,284-29,700 saved seconds. |
| Recorder epochs | Epoch 1 began `2026-08-30 14:57:16 +03:00` (PID 11588); external restart created epoch 2 at `2026-08-31 05:57:47 +03:00` (PID 29372). |
| Retained evidence | Epoch 1: 219-paired-interval monitoring checkpoint, raw log lost at restart. Epoch 2: 213 complete paired intervals, 143.337 control and 143.338 assist-only bot-hours. |
| Exclusions | Epoch 2 excluded 29 startup/partial-sample fragments. Setup run excluded. No bot excluded from the primary intent-to-treat result. |

The exact-run epoch-2 evidence, live configuration lines, durable endpoints, full log snapshot, and
analysis were captured outside the repository at
`C:\Users\Karri\AppData\Local\Temp\lazy-questing-cohort-11-atomic-in-range-assist-vs-control-endpoint-20260831-100133`.
The manifest SHA-256 is
`d280724942e23c0a9402eb03968dee23f65a0a84f1942aea50753ba1e8c7e014`; the exact-run extract has
1,210 lines and SHA-256
`ffb87ee93e03bae9a7a151118afd7eb9b823fd7f99cd142214f5bae252a5b631`.
Raw runtime data was not committed.

## Hypothesis and treatment contract

Atomic pickup and turn-in interactions already within interaction distance should convert
incidental proximity into quest XP and reward gear without spending travel time or displacing
grinding, looting, or RPG/service activity.

Treatment may:

- scan for valid pickups and completed turn-ins only within `INTERACTION_DISTANCE`;
- make one immediate accept/talk interaction attempt;
- repeat discovery on the configured 30-second interval.

Treatment may not:

- acquire an intent, strategy ownership, or an intent leg;
- create or replace a travel target, route toward quest work, or suppress `new rpg`;
- act during combat, death, flight, teleport, pending loot, essential service, or active RPG work.

Primary promotion gates were at least 10 atomic successes with at least 50% conversion; median
time to the latest well-sampled common level at least 5% faster; XP per bot-hour no more than 2%
lower; no late window or level band more than 5% lower; preserved loot and equal-level gear;
travel below approximately +3 percentage points; and effectively zero hard stalls or catastrophic
class/race tails.

## Assignment and exposure integrity

Assignment and recorder exposure passed. The seed reproduced the module's unsigned 64-bit hash,
yielded exactly 20 bots per arm in each faction, exact splits for even race/class cells, and a
one-bot difference for odd cells. The locked roster SHA-256 was
`3fa7b5c7ef10c7afda8250d6162ad4fd480248f377817979a406b0161823ca54`.
All 213 retained epoch-2 scheduler, flight, and gear intervals contained the expected 40/40 arms,
480 gear samples per arm, and no control active-intent violation.

| Category | Total | Control | Assist-only |
| --- | ---: | ---: | ---: |
| Alliance | 40 | 20 | 20 |
| Horde | 40 | 20 | 20 |
| Warrior / Hunter / Rogue / Priest | 43 | 22 | 21 |
| Shaman / Mage / Warlock / Druid / Paladin | 37 | 18 | 19 |

## Implementation sanity check

Structural non-interference passed, but activation failed. The launch epoch recorded two atomic
pickup acquisitions, both progressed and succeeded at 3.5-yard average and 4-yard maximum range.
No later atomic acquisition or ending appeared, including none in all 1,210 exact-run epoch-2
lines. Active assist-only and control intents remained zero; protected, lease, exhausted,
hard-stall, cancelled, repoint, preemption, and budget-limited counters remained zero.

Thus success conversion was 100% (2/2), but only 2 successes were observed versus the required
minimum of 10. The treatment was effectively dormant for almost the entire exposure.

## Flight-recorder results

Only epoch 2 has preserved raw lines and is the auditable recorder table below. Rates are aggregate
events divided by observed bot-hours; the comparison is assist-only relative to control.

| Metric | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| XP per bot-hour | 2,841.68 | 2,827.48 | -0.50% |
| Kill XP per bot-hour | 2,533.88 | 2,495.41 | -1.52% |
| Quest XP per bot-hour | 256.70 | 275.75 | +7.42% |
| Kills per bot-hour | 47.85 | 46.85 | -2.07% |
| Loot events per bot-hour | 17.36 | 21.58 | +24.32% |
| Loot events per kill | 0.363 | 0.461 | +26.95% |
| Quest pickups per bot-hour | 1.046 | 1.102 | +5.33% |
| Objective deltas per bot-hour | 4.130 | 5.065 | +22.63% |
| Quest completions per bot-hour | 0.733 | 0.851 | +16.19% |
| Quest turn-ins per bot-hour | 0.586 | 0.614 | +4.76% |
| Level-ups per bot-hour | 0.433 | 0.460 | +6.45% |
| Deaths per bot-hour | 8.121 | 7.360 | -9.36% |

The isolated XP gate passed in epoch 2, but the treatment did not cause enough interactions to
explain the quest/loot differences. The final epoch-1 monitoring checkpoint also kept XP inside
the gate (+1.88%) but disagreed on loot: loot events were 5.54% lower and loot per kill 10.07%
lower. Because the restart erased epoch-1 raw lines, these epoch summaries are disclosed
separately and are not merged into a false exact total.

## Activity mix

| Activity | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Travel | 22.99% | 22.87% | -0.13 pp |
| Fight | 28.32% | 27.47% | -0.85 pp |
| Loot | 5.37% | 5.15% | -0.22 pp |
| Interact | 0.28% | 0.20% | -0.08 pp |
| Service | 0.00% | 0.00% | 0.00 pp |
| Dead/corpse | 3.50% | 3.18% | -0.32 pp |
| Idle | 39.54% | 41.13% | +1.59 pp |

The travel gate passed comfortably. Assist-only traded small reductions in fight, loot, interact,
and dead time for idle time, consistent with random behavioral variation or base Playerbots
behavior rather than a two-event treatment effect.

## Equipment progression

The primary equal-level comparisons are shown at level 9, the latest common well-sampled band.
Current saved rows at level 9 had 17 control and 18 assist-only bots; attained-level milestone
item level had 31 and 34. Saved online rows can lag, so these are secondary to the durable
milestones and should not be confused with a live Aquarium snapshot.

| Metric at player level 9 | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Milestone item level | 4.032 (n=31) | 4.000 (n=34) | -0.80% |
| Populated-slot average item level | 4.388 | 4.430 | +0.95% |
| Total equipped item-level points | 37.88 | 39.33 | +3.83% |
| Occupied equipment slots | 8.71 | 8.78 | +0.83% |
| Weapon item level | 5.18 | 6.00 | +15.91% |
| Weapon quality | 0.882 | 0.833 | -5.56% |

| Recorder equipment metric, epoch 2 | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Equippable loot per bot-hour | 1.591 | 2.253 | +41.67% |
| Equippable quest rewards per bot-hour | 0.112 | 0.133 | +18.75% |
| Equip events per bot-hour | 0.460 | 0.635 | +37.88% |
| Loot-gear average item level | 5.461 | 5.232 | -4.18% |
| Quest-reward average item level | 7.063 | 5.947 | -15.79% |
| Equipped-event average item level | 6.667 | 6.571 | -1.43% |

Quantity improved in the preserved epoch, but quality did not. The strict equal-level gear gate
therefore failed: level-9 weapon quality and milestone item level were lower, and current level-10
populated-slot item level and weapon quality were also lower (-4.08% and -10.26%).

## Database endpoint

All 80 bots reached the common 25,200-second exposure. The table uses saved current rows captured
after monitoring overshot the endpoint, so the level-9 right-censored milestone analysis remains
the primary progression result.

| Metric | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Level, mean (median) | 9.375 (9) | 9.400 (9) | +0.025; Welch 95% [-0.427, +0.477] |
| Level-9 KM median played time | 21,170 s | 20,366 s | 3.80% faster |
| Rewarded quests, mean (median) | 11.05 (10.5) | 10.88 (11.5) | -1.58%; mean CI [-1.91, +1.56] |
| Money, copper, mean (median) | 2,101.93 (2,065.5) | 2,119.23 (2,257.5) | +0.82%; mean CI [-437.84, +472.44] |
| Total progression XP, mean | 27,408.98 | 27,780.65 | +1.36% |
| XP per played hour, mean (median) | 3,396.31 (3,314.64) | 3,438.39 (3,411.66) | +1.24% |

No SOAP credentials were available after the external restart, so no persistent live Aquarium
roster was captured. All saved rows were online; milestone times and attained levels come from
`strict_altbot_levelups` and are not subject to the online `characters`-row lag caveat.

## Time and level-band trends

| Window or level band, epoch 2 | XP difference | Quest XP difference | Loot difference | Loot-gear rate difference |
| --- | ---: | ---: | ---: | ---: |
| First 5 paired intervals | -26.64% | -16.69% | -4.97% | -57.15% |
| Last 5 paired intervals | -13.87% | -100.00% | +13.06% | -16.65% |
| Level 7 to 8 | -12.44% | -62.18% | +22.12% | +61.62% |
| Level 8 to 9 | +4.76% | +38.67% | +23.26% | +33.05% |
| Level 9 to 10 | +2.49% | +9.67% | +31.62% | +46.68% |

The last five intervals are noisy (about 3.37 bot-hours per arm), but the preregistered rule did
not permit a late window or level band worse than -5%. Both the last window and level-7 band fail
that gate. Later level bands recover, which is encouraging as descriptive evidence but does not
rescue the dormant treatment.

Milestone median improvements were +5.00% to level 7, +0.64% to level 8, and +3.80% to level 9.
The latest well-sampled common level therefore fails the required reproducible +5% improvement.

## Intent and scheduler health

| Metric | Control | Assist-only |
| --- | ---: | ---: |
| Atomic acquisitions | 0 | 2 |
| Atomic successful endings | 0 | 2 |
| Active Lazy Questing intents | 0 | 0 |
| Protected / lease / exhausted / cancelled | 0 | 0 |
| Scheduler repoints / preemptions | 0 | 0 |
| Recorder hard stalls | 0 | 0 |

The two acquisitions were valid in-range pickups and succeeded. The absence of any further
attempts is the dominant mechanism finding: the selector almost never saw an eligible in-range
pickup or turn-in, or the eligibility/hook accounting path suppressed later candidates.

## Robustness, tails, and caveats

- The primary analysis includes all 80 randomized bots. Qivau (GUID 2605, assist-only) remained at
  level 6 and fixed XP/position for hours. Excluding Qivau leaves the level-9 Kaplan-Meier median
  unchanged at 20,366 seconds (34/39 reach rather than 34/40), so the sensitivity still improves
  only 3.80%.
- Beetsu (assist-only) and Nasta (control) also held fixed XP and position for hours; Ahoord
  (assist-only) held XP while moving locally, and Alygyael (control) became fixed late. The
  recorder's scheduler hard-stall counter remained zero because these were base-bot stalls rather
  than owned Lazy Questing intents. The effective-zero-hard-stalls gate fails operationally.
- At level 9, the largest adverse race cells were Draenei (+24.38% mean played time) and Human
  (+12.20%); the largest adverse class cell was Warlock (+6.80%). Cells are small and conditioned
  on attaining level 9, but they fail the no-catastrophic-tail requirement as a safety signal.
- The epoch-1 raw log was reset by an external worldserver restart before preservation. Its last
  219-interval monitoring summary is useful corroboration, not an auditable exact aggregate and
  is never silently combined with epoch 2.
- Durable milestones are right-censored at exactly 25,200 played seconds. Recorder epoch 2 and
  saved current rows include the unavoidable monitoring tail through capture and are secondary.
- Welch intervals are diagnostic only because bots shared one world. The run cannot distinguish
  ordinary seed/world variance from any treatment effect when only two treatment events occurred.

## Decision and next experiment

Do not promote or repeat this treatment as an outcome cohort. It failed activation (2 versus 10
required successes), median time-to-level (3.80% versus 5%), late/level-band stability, strict
equal-level gear quality, operational stalls, and tail safety. Epoch-2 XP and travel gates passed,
and loot quantity improved, but those outcomes are not attributable to a nearly dormant feature.

The single next uncertainty is why eligible in-range interactions disappear after launch. First
use the preserved recorder evidence and a read-only code/path audit to separate four possibilities:
no nearby eligible quest givers, over-restrictive protection/eligibility checks, the 30-second
discovery cadence missing short proximity windows, or successful hooks not being counted. Then run
a short, fresh, balanced diagnostic cohort with counter-only instrumentation for scanned nearby
objects and each rejection reason. Only after at least 10 real atomic successes at at least 50%
conversion should another seven-hour promotion cohort test XP, time-to-level, loot, and equal-level
gear.
