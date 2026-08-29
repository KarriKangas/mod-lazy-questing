# Cohort 9: assist-only vs control

## Snapshot verdict

Assist-only is substantially healthier than the full current treatment, but it is not a clear overall progression win. It increases quest XP, quest completions, turn-ins, and pickups while reducing deaths and idle time. It also reduces fighting, kills, loot, and total XP. The late-run gap widened, so this should be considered a promising isolation result that needs another run or a targeted fix—not a production recommendation yet.

## Run metadata

| Field | Value |
| --- | --- |
| Date | 2026-08-28 |
| Cohort | Cohort 9 |
| Population | 80 fresh level-1 strict Altbots |
| Factions | 40 Alliance, 40 Horde |
| Experiment arms | 40 control, 40 assist-only |
| Seed | `4616801` |
| Configuration | control 50%, assist-only 50%, current 0% |
| Observation | 423 paired one-minute recorder intervals, approximately 7.05 wall-clock hours / 282 bot-hours per arm |
| Exposure integrity | every scheduler and flight-recorder interval contained 40 control and 40 assist-only bots |
| Database endpoint | all 80 active bots online at the snapshot |

The seed was selected from the active roster using the module's stable GUID/race/class hash. It produced exactly 20 control and 20 assist-only bots per faction, with the mathematically closest possible split for every race and class (even-sized categories exact; odd-sized categories differ by one).

## Flight-recorder results

Rates are aggregate events divided by observed bot-hours. The comparison is assist-only relative to control.

| Metric | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| XP per bot-hour | 3,768.51 | 3,522.74 | **-6.5%** |
| Kill XP per bot-hour | 3,317.68 | 3,054.94 | -7.9% |
| Quest XP per bot-hour | 375.67 | 388.99 | **+3.5%** |
| Kills per bot-hour | 67.80 | 64.15 | -5.4% |
| Loot events per bot-hour | 25.84 | 22.21 | -14.0% |
| Quest pickups per bot-hour | 2.14 | 2.19 | +2.3% |
| Objective deltas per bot-hour | 6.89 | 6.50 | -5.7% |
| Quest completions per bot-hour | 1.18 | 1.23 | **+4.2%** |
| Quest turn-ins per bot-hour | 0.99 | 1.08 | **+9.1%** |
| Level-ups per bot-hour | 0.83 | 0.80 | -3.6% |
| Deaths per bot-hour | 5.83 | 4.79 | **-17.8%** |

The treatment is doing the intended narrow thing: it gets slightly more quest reward activity without objective routing. It does not recover the combat/loot throughput lost by the full treatment.

## Activity mix

| Activity | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| Travel | 23.5% | 32.3% | +8.8 percentage points |
| Fight | 29.2% | 26.6% | -2.6 pp |
| Loot | 8.5% | 8.6% | +0.1 pp |
| Dead/corpse | 2.3% | 2.0% | -0.4 pp |
| Idle | 36.2% | 30.2% | -6.0 pp |

Assist-only converts idle time into more travel, but that travel still has a modest productivity cost. Unlike Cohort 8, the recorder shows no current-arm objective-routing behavior: control has zero intents and assist-only has the expected active intents.

## Database endpoint

At the snapshot, all 40 bots in each arm were online:

| Metric | Control mean | Assist-only mean | Difference |
| --- | ---: | ---: | ---: |
| Level | 9.80 | 9.55 | -0.25 levels |
| Rewarded quests | 10.43 | 11.07 | +0.65 quests |
| Money, copper | 2,225 | 2,275 | +49 copper |

The level difference is small relative to bot-to-bot spread. An approximate Welch 95% interval for assist-only minus control level was -0.64 to +0.14 levels; the corresponding intervals for rewarded quests and money also crossed zero. This is evidence of a near-neutral endpoint, not a statistically established improvement.

## Time trend

The aggregate average hides a late deterioration:

| Window | XP difference | Quest XP difference | Turn-in difference |
| --- | ---: | ---: | ---: |
| First 30 minutes | +4.7% | -10.3% | -14.3% |
| Last 30 minutes | -17.0% | -30.9% | -30.8% |

Level-up rate in the last 30-minute window was 41.7% lower for assist-only. That late pattern is the main reason not to call the experiment a win despite the positive full-window quest metrics.

## Scheduler health

Across the 423 scheduler intervals the run recorded 4,098 preemptions, 1,453 exhausted intents, and only 7 hard stalls. The hard-stall count is dramatically lower than Cohort 8's full-treatment run, supporting the hypothesis that objective routing—not quest pickup/turn-in assistance alone—is the main source of the earlier catastrophic stalls. Intent churn remains high enough to warrant follow-up.

## Follow-up item-level snapshot

Checked on 2026-08-29 after the cohort had progressed to approximately level 10. The server had restarted shortly before this check, so the flight recorder had reset, but all 80 Cohort 9 bots were online and both the live Aquarium roster and durable level-up history were available.

The live Aquarium roster computes rounded average item level over populated equipment slots. Control averaged 4.75 item level (median 5, range 3-7) at mean player level 10.13. Assist-only averaged 4.55 (median 4, range 2-7) at mean player level 9.93. The raw assist-minus-control difference was -0.20 item level; an approximate Welch 95% interval was -0.69 to +0.29, so the overall difference is not established.

The database field is a level-up snapshot rather than a continuously refreshed value. Each row records the same equipped-slot average when the bot gains that level. The latest saved snapshots told the same broad story: 4.58 control versus 4.33 assist-only, a -0.25 difference with an approximate interval of -0.73 to +0.23.

Equal-level comparisons exposed a possible late divergence:

| Current level | Control | Assist-only | Difference |
| --- | ---: | ---: | ---: |
| 9 | 4.33 (n=9) | 4.71 (n=7) | +0.38 |
| 10 | 4.67 (n=18) | 4.89 (n=19) | +0.23 |
| 11 | 5.25 (n=12) | 4.09 (n=11) | -1.16 |

The durable milestone rows show a similar pattern: arms were essentially even through level 9, while assist-only was 0.44 item level lower at the level-10 snapshot and 1.10 lower at level 11. The level-11 sample is small and contains one assist-only outlier at item level 2, but the gap is not caused by that bot alone. This is consistent with the recorder's 14% lower loot-event rate under assist-only, although the data do not prove that reduced looting caused the gear difference.

Quest-reward composition explains why more completed quests did not automatically produce more gear. By the follow-up snapshot, control had completed 472 rewarded quests and assist-only 510. However, only 140 of the assist-only quests offered any equippable reward, versus 145 for control. Gear-reward quests were 27.5% of assist-only completions and 30.7% of control completions. The additional assist-only completions were therefore disproportionately XP, money, consumable, or delivery quests rather than gear opportunities.

Equipment-slot coverage was effectively identical: 8.68 populated slots per control bot and 8.60 per assist-only bot. Control nevertheless had 41.35 total equipped item-level points per bot versus 39.15 for assist-only. Among level-11 bots, assist-only actually occupied more slots (9.22 versus 8.67) but carried lower-level pieces (40.00 versus 45.67 total item-level points). The observed average-item-level gap is therefore not an artifact of empty slots being excluded from the average.

## Decision

Keep assist-only as a viable component, but do not ship it as a blanket progression improvement yet. The next engineering test should either (a) add a progress watchdog and service-preemption release to assist-only, or (b) run a second clean assist-only cohort to determine whether the late deficit repeats. The full current objective-routing treatment remains rejected until its stall and productivity failure is fixed.
