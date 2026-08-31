# Cohort 10: conservative local assist vs control

## Verdict

Reject this treatment and do not promote it. Meaningful activation failed: across the preserved
recorder fragments, 11 assist-only intents were acquired, only one already-in-range turn-in
succeeded, and 10 yielded to protected RPG activity. At the fixed 25,200-second endpoint, the
level-9 Kaplan-Meier median improved by only 0.85% (unchanged when Zurmos is excluded), far short of
the 5% feature goal. The partial recorder kept XP within the component gate (-1.00%), but loot per
kill (-2.20%), equippable loot (-6.90%), equip events (-14.29%), and equal-level gear all worsened.
The treatment therefore fails activation, progression, loot, and gear gates; the next cohort should
isolate atomic in-range pickup/turn-in interactions without acquiring travel ownership.

## Run metadata

| Field | Value |
| --- | --- |
| Date | 2026-08-29 to 2026-08-30 |
| Cohort | Cohort 10 |
| Run ID | `cohort-10-conservative-assist-vs-control` |
| Population | 80 fresh level-1 strict Altbots |
| Guilds | Cohort 10 Alliance (40), Cohort 10 Horde (41) |
| Factions | 40 Alliance, 40 Horde |
| Experiment arms | 40 control, 40 conservative assist-only |
| Seed | `54529878` |
| Configuration | control 50%, assist-only 50%, current 0% |
| Code/build | `a935e592f1a9`; recorder/report tooling was uncommitted, treatment code matched this build |
| Observation | fixed 25,200 played seconds; 290 preserved complete paired intervals and 193.35 observed bot-hours per arm |
| Recorder epochs | starts at 2026-08-29 12:01:07, 19:47:59, and 2026-08-30 12:49:12 Europe/Helsinki |
| Exclusions | one startup fragment per preserved epoch; unpreserved post-checkpoint tails after external log resets |

The cohort launched at 11:44:16 and was fully online by 11:44:37 on August 29. The endpoint was
enforced as played exposure, not wall time. At the final restart the slowest non-Zurmos durable
floor was 24,754 seconds; eight complete 40/40 intervals added 483.475 observed seconds per bot,
placing the conservative floor at 25,237.475 seconds before capture. The captured third epoch
contains nine retained intervals because another interval completed during preservation.

Raw evidence remains outside the repository at
`C:\Users\Karri\AppData\Local\Temp\lazy-questing-cohort-10-conservative-assist-vs-control-20260830-1300-epoch3-endpoint`.
Its `SHA256SUMS.txt` hash is
`6DBCF19E38753940E78489D7513DDE7ECB96D9C843F95D3DEEDF4CB2EECF4D26`.

## Hypothesis and treatment contract

Nearby pickup and turn-in assistance should add quest rewards without displacing combat, loot, or
equipment progression when it can act only on null, exploration, or inactive targets.

Treatment may:

- pick up a quest within 200 yards;
- turn in a completed quest within 250 yards;
- intervene only on null, exploration, or inactive/expired targets.

Treatment may not:

- route to objectives or replace an active grind target;
- replace RPG/service activity or remove `new rpg`;
- acquire or retain an intent while loot is pending.

Primary promotion gates:

- XP per bot-hour no more than 2% below control in this component test;
- no late level band more than 5% below control and at least 5% faster median time to level for the
  feature goal;
- loot per kill and equal-level gear no worse than control;
- travel increase below approximately 3 percentage points, effectively zero hard stalls, and no
  catastrophic class/race tail.

The primary analysis is intent to treat. Zurmos (`guid 2537`, assist-only) was predeclared as a
catastrophic-tail sensitivity case but was never manually recovered or removed from the headline.

## Assignment and exposure integrity

Assignment was exactly 20/20 within each faction. Every even race/class category split exactly;
odd categories differed by one, the mathematical optimum. Every retained scheduler, flight, and
gear interval had 40 control and 40 assist-only bots, and control recorded no active treatment
intent.

| Category | Total | Control | Assist-only |
| --- | ---: | ---: | ---: |
| Alliance | 40 | 20 | 20 |
| Horde | 40 | 20 | 20 |
| Human / Orc / Dwarf / Night Elf | 18 / 10 / 6 / 10 | 9 / 5 / 3 / 5 | 9 / 5 / 3 / 5 |
| Undead / Tauren / Gnome / Troll | 3 / 12 / 4 / 7 | 2 / 6 / 2 / 3 | 1 / 6 / 2 / 4 |
| Blood Elf / Draenei | 8 / 2 | 4 / 1 | 4 / 1 |
| Warrior / Paladin / Hunter | 14 / 9 / 10 | 7 / 5 / 5 | 7 / 4 / 5 |
| Rogue / Priest / Shaman | 6 / 7 / 2 | 3 / 3 / 1 | 3 / 4 / 1 |
| Mage / Warlock / Druid | 10 / 10 / 12 | 5 / 5 / 6 | 5 / 5 / 6 |

The 290 intervals cover about 69% of the intended 280 bot-hours per arm. Durable milestones cover
the full played exposure; recorder mechanism estimates do not. Epoch 1 preserves 261 intervals to
16:19, epoch 2 preserves 20 intervals to 20:10, and epoch 3 preserves nine endpoint intervals.

## Implementation sanity check

Activation failed. The first checkpoint recorded 10 acquisitions (three pickup, seven turn-in):
nine ended as protected activity within about 5-10 seconds and only a one-yard turn-in succeeded.
The endpoint epoch added one 22-yard turn-in acquisition that was protected after 10 seconds.
There were no lease expirations, exhausted intents, or hard stalls.

Per-bot debug evidence identifies intent churn as the mechanism. Assist-only leaves `new rpg`
enabled; after Lazy Questing acquires a null/inactive target, `new rpg` selects a grind destination,
makes `rpgInfo` non-idle, and `HasProtectedActivity` releases the new intent. The contract remains
safe, but routed assistance is mostly inert, so outcome differences cannot be attributed to a
meaningfully delivered treatment.

## Flight-recorder results

These rates merge the 290 preserved paired intervals only. They are diagnostic, not a complete
seven-hour recorder estimate.

| Metric | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| XP per bot-hour | 3,907.94 | 3,868.96 | -1.00% |
| Kill XP per bot-hour | 3,386.02 | 3,381.22 | -0.14% |
| Quest XP per bot-hour | 442.38 | 415.06 | -6.18% |
| Kills per bot-hour | 71.91 | 72.57 | +0.92% |
| Loot events per bot-hour | 31.45 | 31.04 | -1.30% |
| Quest pickups per bot-hour | 2.97 | 2.75 | -7.49% |
| Objective deltas per bot-hour | 9.90 | 8.75 | -11.70% |
| Quest completions per bot-hour | 1.70 | 1.53 | -9.76% |
| Quest turn-ins per bot-hour | 1.54 | 1.44 | -6.06% |
| Level-ups per bot-hour | 1.28 | 1.25 | -2.43% |
| Deaths per bot-hour | 3.98 | 3.98 | -0.00% |

The XP component gate passes narrowly, but none of the quest-throughput counters indicate the
expected proximal benefit. Combat was essentially unchanged, while progression events and loot
were slightly lower.

## Activity mix

| Activity | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Travel | 21.26% | 21.05% | -0.21 pp |
| Fight | 27.45% | 26.55% | -0.90 pp |
| Loot | 9.15% | 9.74% | +0.60 pp |
| Interact | 0.33% | 0.37% | +0.04 pp |
| Service | 0.00% | 0.00% | 0.00 pp |
| Dead/corpse | 1.59% | 1.62% | +0.03 pp |
| Idle | 40.23% | 40.67% | +0.44 pp |

Travel passes the gate, but reduced fight share and increased idle/loot share did not produce
better loot throughput or gear.

## Equipment progression

Equal-level saved equipment is the primary gear comparison. Levels 9 and 10 both have useful arm
counts; the same pattern appears in the level-9 milestone item-level snapshot.

| Metric | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Saved player level, mean (median) | 9.20 (9) | 9.20 (9) | 0.00% |
| Level 9 populated-slot item level (n=19/20) | 4.821 | 4.475 | -7.18% |
| Level 9 total equipped item-level points | 40.11 | 36.25 | -9.61% |
| Level 9 occupied slots | 8.37 | 8.15 | -2.61% |
| Level 9 weapon item level | 4.63 | 5.00 | +7.95% |
| Level 10 populated-slot item level (n=16/16) | 4.757 | 4.346 | -8.64% |
| Level 10 total equipped item-level points | 41.50 | 37.50 | -9.64% |
| Level 10 occupied slots | 8.75 | 8.56 | -2.14% |
| Level 10 weapon item level | 5.75 | 5.13 | -10.87% |
| Equippable loot per bot-hour | 3.750 | 3.491 | -6.90% |
| Equippable quest rewards per bot-hour | 0.595 | 0.538 | -9.57% |
| Equip events per bot-hour | 1.557 | 1.334 | -14.29% |

At attained level 9, milestone item level was 4.314 control versus 4.028 assist-only (-6.64%,
Welch mean-difference interval -0.65 to +0.08). Weapon results are mixed, but populated-slot item
level, total points, occupied slots, gear acquisition, and equip events all fail preservation.

Excluding Zurmos improves the saved treatment means but does not change the decision: populated-slot
item level remains -6.94%, total equipped points -8.50%, occupied slots -2.21%, and weapon item
level -1.56% versus control.

## Database endpoint

| Metric | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Level, mean (median) | 9.20 (9) | 9.20 (9) | 0.00; 95% interval -0.40 to +0.40 |
| Kaplan-Meier time to level 9 | 19,082 s | 18,920 s | 0.85% faster |
| Rewarded quests, mean (median) | 11.25 (11.5) | 10.78 (11) | -4.22%; interval -2.18 to +1.23 |
| Money, copper, mean (median) | 2,059.5 (2,174.5) | 1,980.2 (2,141) | -3.85%; interval -411.7 to +253.1 |
| Total progression XP, mean (median) | 26,175 (26,276) | 26,399 (26,483) | +0.86% |
| XP per played hour, mean (median) | 3,669 (3,720) | 3,692 (3,675) | +0.62% mean; -1.19% median |

At level 9, 35/40 control and 36/40 assist-only bots attained the milestone by the endpoint. The
intent-to-treat Kaplan-Meier result is only 0.85% faster, and excluding Zurmos produces the same
median and improvement. Without Zurmos, mean total XP is +2.96%, but the median XP/hour is
effectively unchanged and gear still fails.

Attained-level distribution at capture was control: level 7=2, 8=2, 9=20, 10=16; assist-only:
level 5=1, 7=1, 8=2, 9=18, 10=16, 11=2. Aquarium was not collected because loopback SOAP
credentials were unavailable; `strict_altbot_levelups` is authoritative for milestones, while
online `characters` values may lag in-memory state.

## Time and level-band trends

| Window or level band | XP difference | Quest XP difference | Loot difference | Gear difference |
| --- | ---: | ---: | ---: | ---: |
| All preserved intervals (290) | -1.00% | -6.18% | -1.30% | equip events -14.29% |
| Endpoint epoch, nine intervals | +10.02% | -37.29% | +3.67% | final avg item level -10.42% |
| Recorder level 9 in endpoint epoch | +10.02% | -37.29% | +3.67% | final avg item level -10.42% |
| Durable level-9 milestone | n/a | quests -5.30% | n/a | item level -6.64% |

The endpoint snippet is favorable for combat XP but sharply worse for quest XP and gear. Because
the epoch-1 tail after 16:19 and epoch-2 tail after 20:10 were lost on external log resets, the
preregistered full late-window/level-band gate is not auditable and is not counted as passed. The
durable endpoint still rules out the desired 5% median time-to-level benefit.

## Intent and scheduler health

| Metric | Control | Assist-only |
| --- | ---: | ---: |
| Acquired intents | 0 | 11 |
| Successful endings | 0 | 1 |
| Protected/preempted | 0 | 10 |
| Lease expirations | 0 | 0 |
| Exhausted intents | 0 | 0 |
| Hard stalls | 0 | 0 |

The first 10 acquisitions were three pickups and seven turn-ins; the sole success was a one-yard
turn-in. The third-epoch acquisition was a 22-yard turn-in and was protected after 10 seconds.
Selector and scheduler output showed no persistent budget problem, and every retained interval
reported the expected arm counts.

## Robustness, tails, and caveats

- Zurmos stalled at level 4 and 1,838 XP while targeting a Bristleback Shaman on an inaccessible
  vertical layer. No Lazy Questing intent or manual recovery occurred. The external restart
  dislodged him naturally; he attained level 5 at 17,041 played seconds and ended saved at level 5,
  100 XP, position `(-2954.93, -912.8, 68.64)`. He remains in the headline.
- Excluding Zurmos leaves the level-9 median improvement at 0.85% and leaves all main gear means
  below control, so the failed verdict is not an outlier artifact.
- Level-9 class tails are mixed: Mage and Priest treatment means were 5.1% and 7.7% slower, while
  Hunter and Paladin were faster. Several small race cells also exceeded 5% in both directions.
  This is not a stable or promotable cross-category result.
- Bots share a world, so Welch intervals are diagnostic rather than independent-unit inference.
- The two external resets destroyed substantial raw recorder tails. Preserved mechanism evidence
  covers 69% of fixed exposure; durable milestone and saved endpoint evidence covers the cohort,
  but a complete late-window activity decomposition is impossible.
- The outcome cannot distinguish a real but tiny treatment effect from balanced-seed noise because
  the treatment itself almost never retained ownership long enough to act.

## Decision and next experiment

Reject the routed conservative assist treatment. Gate outcomes are: activation fail; XP component
pass (-1.00%); 5% median time-to-level fail (+0.85%); loot fail; gear fail; travel pass; hard-stall
pass; late-window gate not auditable. No second-seed promotion repeat is warranted.

Recorder evidence isolates the remaining mechanism as intent arbitration/churn, not excessive
travel: `new rpg` claims a grind destination immediately after acquisition and the protection guard
then releases the assist intent. The next single-variable treatment should scan only within
`INTERACTION_DISTANCE`, perform an atomic pickup or turn-in without `AcquireIntent`,
`AcquireStrategyOwnership`, or `AssignIntentLeg`, and retain all combat, death, flight, teleport,
pending-loot, vendor/repair, and active-RPG guards. Test it in a fresh, exactly balanced 80-bot
cohort with a new seed and the same 25,200-second endpoint. Promotion still requires at least 5%
faster median time to level across seeds, XP no more than 2% lower in the component test, no late
band more than 5% lower, loot/gear preserved, travel below +3 pp, and effectively zero hard stalls.
