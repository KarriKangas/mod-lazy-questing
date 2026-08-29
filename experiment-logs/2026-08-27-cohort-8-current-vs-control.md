# Cohort 8: full Lazy Questing vs control

## Verdict

Full Lazy Questing was materially worse than control over this clean run. The treatment did activate and changed behavior in the intended proximal direction: bots travelled much more and idled less. That activity did not translate into productive progression. Treatment bots earned less XP, completed fewer quest objectives and turn-ins, looted less, levelled more slowly, and accumulated about half as much money.

Do not promote the full treatment on the strength of this implementation. The next experiment should isolate pickup and turn-in assistance from objective routing by comparing `assist-only` with control.

## Run metadata

| Field | Value |
| --- | --- |
| Date | 2026-08-27 |
| Cohort | Cohort 8 |
| Population | 80 new level-1 strict Altbots |
| Factions | 40 Alliance, 40 Horde |
| Experiment arms | 40 control, 40 current |
| Seed | `4179573` |
| Configuration | control 50%, assist-only 0%, current 50% |
| Clean recorder start | worldserver restart at approximately 16:01 Europe/Helsinki |
| Headline rate window | 142 complete paired one-minute samples |
| Exposure | approximately 94.6 bot-hours per arm |
| Assignment integrity | every complete recorder interval contained 40 control and 40 current bots |

Assignment used the module's stable hash of character GUID, race, class, and seed. It was exactly balanced by faction and treatment arm. Race and class were exact or within one bot per arm except Dwarf (2 control, 4 current) and Paladin (3 control, 1 current).

## Flight-recorder results

Rates below are aggregate events divided by observed bot-hours.

| Metric | Control | Current | Current vs control |
| --- | ---: | ---: | ---: |
| Total XP | 391,289 | 321,126 | -17.9% per bot-hour |
| XP per bot-hour | 4,136.64 | 3,394.78 | -17.9% |
| Kill XP per bot-hour | 3,570.94 | 2,991.58 | -16.2% |
| Quest XP per bot-hour | 525.00 | 392.68 | -25.2% |
| Kills per bot-hour | 83.21 | 76.61 | -7.9% |
| Loot events per bot-hour | 46.07 | 30.37 | -34.1% |
| Quest pickups per bot-hour | 4.79 | 3.65 | -23.8% |
| Objective deltas per bot-hour | 14.81 | 11.39 | -23.1% |
| Quest completions per bot-hour | 2.93 | 2.42 | -17.4% |
| Quest turn-ins per bot-hour | 2.66 | 2.15 | -19.2% |
| Level-ups per bot-hour | 2.18 | 1.92 | -11.9% |
| Deaths per bot-hour | 0.76 | 0.23 | -69.7% |

The lower death rate is the sole clear outcome win. It is accompanied by less fighting and substantially lower progression, so it is more consistent with reduced productive exposure than superior questing.

## Activity mix

| Activity | Control | Current |
| --- | ---: | ---: |
| Travel | 20.3% | 39.6% |
| Fight | 21.6% | 17.0% |
| Loot | 11.8% | 12.4% |
| Interact | 0.6% | 0.3% |
| Dead/corpse | 0.3% | 0.1% |
| Idle | 45.4% | 30.6% |

The treatment therefore passed the implementation sanity check: control had zero Lazy Questing intents, while current averaged 32.8 active intents and nearly doubled its travel share. The failure is not that the A/B switch was inactive; it is that the movement it created was not productive enough.

## Database endpoint

The per-bot database endpoint agreed with the recorder:

| Metric | Control mean | Current mean | Difference |
| --- | ---: | ---: | ---: |
| Cumulative XP | 9,624.30 | 7,935.70 | -1,688.60 (-17.5%) |
| Level | 6.10 | 5.52 | -0.57 (-9.4%) |
| Rewarded quests | 6.42 | 5.18 | -1.25 (-19.5%) |
| Money, copper | 504.68 | 258.77 | -245.90 (-48.7%) |

An approximate Welch 95% interval for the per-bot cumulative-XP difference was -2,704.93 to -672.27 XP, with Cohen's d of -0.74. These are diagnostic estimates: bots share one game world and are not perfectly independent experimental units.

## Robustness and outliers

Two current-treatment hunters were catastrophically stuck at level 1: Ziwgirvis at approximately 90 cumulative XP and Vudraan at approximately 140. Removing both still left current 13.3% behind in cumulative XP. Removing every hunter from both arms left current 13.0% behind. Every represented class had lower mean cumulative XP under treatment, so the result is not explained by the small class imbalance or the two worst bots.

Performance also degraded rather than converged over time. The current-arm XP deficit moved from 8.4% in minutes 1-30 to 39.0% in minutes 121-145. In that last block quest XP was 46.4% lower, turn-ins were 40% lower, and current-arm idle time had become 11.6% higher than control.

## Failure evidence

The scheduler accumulated 5,135 preemptions, 573 exhausted intents, and 174 hard stalls during the measured run. Logs showed repeated `no alternate quest leg` and hard-stall recovery churn. The two stuck hunters showed quest intents interacting badly with ammo/vendor servicing: they entered low-ammo service behavior and did not recover useful quest or grinding progress.

The most likely fault domain is objective routing and movement ownership, particularly around essential service preemption and failed quest legs. This is a working causal hypothesis, not yet a proven root cause.

## Decision and next experiment

Run a fresh 80-bot cohort with 40 control and 40 `assist-only` bots, balanced by faction, race, and class. Assist-only permits quest pickup and turn-in but does not route bots to objectives. This directly tests whether the useful edges of Lazy Questing can survive without the objective-routing behavior implicated here.

If assist-only is neutral or positive, focus subsequent engineering on objective-route release, service-preemption recovery, and a watchdog that abandons an intent when travel does not produce semantic progress. If assist-only also loses, inspect pickup/turn-in interference before making further routing changes.
