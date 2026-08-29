# Cohort [N]: [treatment] vs [control]

<!-- Copy this file, replace every bracketed field, and remove instructional comments. -->

## Verdict

[State the decision in the first sentence. In two to four more sentences, summarize total
progression, the mechanism that changed, important time/tail behavior, and whether the treatment
passes the preregistered gates.]

## Run metadata

| Field | Value |
| --- | --- |
| Date | [YYYY-MM-DD] |
| Cohort | [Cohort N] |
| Run ID | `[unique configured run ID]` |
| Population | [count and fresh/continued starting state] |
| Guilds | [exact guild names and IDs] |
| Factions | [Alliance count, Horde count] |
| Experiment arms | [control count, treatment count] |
| Seed | `[seed]` |
| Configuration | [control %, assist-only %, current %] |
| Code/build | [commit and relevant uncommitted treatment, if any] |
| Observation | [complete paired intervals, wall-clock span, bot-hours per arm] |
| Recorder epochs | [server start/restart boundaries included] |
| Exclusions | [partial intervals or other exclusions, with reasons] |

## Hypothesis and treatment contract

[One falsifiable sentence stating why this treatment should improve the primary outcome.]

Treatment may:

- [allowed behavior]

Treatment may not:

- [protected behavior/non-interference rule]

Primary promotion gates:

- [XP or time-to-level gate]
- [late-window or level-band gate]
- [gear/loot gate]
- [stall and catastrophic-tail gate]

## Assignment and exposure integrity

[State whether faction, race, and class balance is exact or mathematically closest possible. State
whether every retained interval has the expected arm counts and whether mode assignment agrees
with scheduler output.]

| Category | Total | Control | Treatment |
| --- | ---: | ---: | ---: |
| [Faction/race/class] | [n] | [n] | [n] |

## Implementation sanity check

[Show that control had no treatment intents and that the treatment changed the intended proximal
behavior. Include acquisition distance/type/outcome when available. If activation failed, stop and
make that the headline finding.]

## Flight-recorder results

Rates are aggregate events divided by observed bot-hours. The comparison is treatment relative to
control.

| Metric | Control | Treatment | Difference |
| --- | ---: | ---: | ---: |
| XP per bot-hour | [value] | [value] | [%] |
| Kill XP per bot-hour | [value] | [value] | [%] |
| Quest XP per bot-hour | [value] | [value] | [%] |
| Kills per bot-hour | [value] | [value] | [%] |
| Loot events per bot-hour | [value] | [value] | [%] |
| Quest pickups per bot-hour | [value] | [value] | [%] |
| Objective deltas per bot-hour | [value] | [value] | [%] |
| Quest completions per bot-hour | [value] | [value] | [%] |
| Quest turn-ins per bot-hour | [value] | [value] | [%] |
| Level-ups per bot-hour | [value] | [value] | [%] |
| Deaths per bot-hour | [value] | [value] | [%] |

[Interpret total progression first, then quest/combat/loot/death mechanisms.]

## Activity mix

| Activity | Control | Treatment | Difference |
| --- | ---: | ---: | ---: |
| Travel | [%] | [%] | [percentage points] |
| Fight | [%] | [%] | [pp] |
| Loot | [%] | [%] | [pp] |
| Interact | [%] | [%] | [pp] |
| Service | [%] | [%] | [pp] |
| Dead/corpse | [%] | [%] | [pp] |
| Idle | [%] | [%] | [pp] |

[Explain which activities gained or lost time and whether the exchange was productive.]

## Equipment progression

| Metric | Control | Treatment | Difference |
| --- | ---: | ---: | ---: |
| Live player level | [value] | [value] | [value/%] |
| Populated-slot average item level | [value] | [value] | [value/%] |
| Total equipped item-level points | [value] | [value] | [value/%] |
| Occupied equipment slots | [value] | [value] | [value/%] |
| Weapon item level | [value] | [value] | [value/%] |
| Equippable loot per bot-hour | [value] | [value] | [%] |
| Equippable quest rewards per bot-hour | [value] | [value] | [%] |
| Equip events per bot-hour | [value] | [value] | [%] |

[Prefer same-level comparisons. Distinguish live Aquarium values from durable level-up snapshots;
state sample sizes and whether empty slots or low-quality pieces explain a gap.]

## Database endpoint

| Metric | Control | Treatment | Difference |
| --- | ---: | ---: | ---: |
| Level, mean (median) | [value] | [value] | [value and interval] |
| Played time to attained level | [value] | [value] | [value/%] |
| Rewarded quests | [value] | [value] | [value/% and interval] |
| Money, copper | [value] | [value] | [value/% and interval] |

[Describe agreement or disagreement with the recorder. Note that live `characters` rows can lag
for online bots; Aquarium and `strict_altbot_levelups` take precedence for their respective uses.]

## Time and level-band trends

| Window or level band | XP difference | Quest XP difference | Loot difference | Gear difference |
| --- | ---: | ---: | ---: | ---: |
| First [N] minutes | [%] | [%] | [%] | [value/%] |
| Last [N] minutes | [%] | [%] | [%] | [value/%] |
| Level [N] | [%] | [%] | [%] | [value/%] |

[Say whether the arms converge, remain stable, or diverge.]

## Intent and scheduler health

| Metric | Control | Treatment |
| --- | ---: | ---: |
| Acquired intents | [n] | [n] |
| Successful endings | [n] | [n] |
| Protected/preempted | [n] | [n] |
| Lease expirations | [n] | [n] |
| Exhausted intents | [n] | [n] |
| Hard stalls | [n] | [n] |

[Summarize acquisition distances, durations, previous target categories, selector cost, and any
budget-limited ticks when relevant.]

## Robustness, tails, and caveats

- [Worst bots/classes/races and whether any are genuinely stuck.]
- [Headline result with catastrophic outliers removed, if applicable.]
- [Shared-world dependence, imbalance, restart, logging, or endpoint limitations.]
- [Alternative explanation that the current data cannot distinguish.]

## Decision and next experiment

[Promote, reject, retain as a component, or rerun. Tie the decision explicitly to the promotion
gates. Propose one next cohort that isolates the largest remaining uncertainty, including intended
population, arms, balance, duration/level band, and success criteria.]
