# Cohort 10: conservative local assist vs control

## Status

Running. This file records the pre-registered treatment, balance, measurement contract, and launch
verification. It is not yet an outcome report.

## Hypothesis

Nearby pickup and turn-in assistance can retain the useful quest-reward edge seen in Cohort 9
without reducing combat, loot, or equipment progression when it is forbidden from replacing active
grinding, RPG/service activity, or pending loot and is constrained by short distance and time leases.

## Run metadata

| Field | Value |
| --- | --- |
| Date | 2026-08-29 |
| Run ID | `cohort-10-conservative-assist-vs-control` |
| Population | 80 fresh level-1 strict Altbots |
| Guilds | Cohort 10 Alliance (40), Cohort 10 Horde (40) |
| Arms | 40 control, 40 conservative assist-only |
| Seed | `54529878` |
| Configuration | control 50%, assist-only 50%, current 0% |
| Final server start | 11:44:16 Europe/Helsinki |
| Full roster online | by 11:44:37 Europe/Helsinki |
| Assignment method | stable GUID/race/class hash |

The cohort was created during a short 100%-control setup phase so its actual roster could be used to
search for a balanced seed. All 80 bots were still level 1 when the final seed was locked. The
worldserver was then restarted, clearing the flight recorder and activating the final 50/50 run.

## Assignment balance

Both factions are exactly 20 control / 20 assist-only. Every even-sized race and class is split
exactly in half; odd-sized categories differ by one, which is the mathematical optimum.

| Race | Total | Control | Assist-only |
| --- | ---: | ---: | ---: |
| Human | 18 | 9 | 9 |
| Orc | 10 | 5 | 5 |
| Dwarf | 6 | 3 | 3 |
| Night Elf | 10 | 5 | 5 |
| Undead | 3 | 2 | 1 |
| Tauren | 12 | 6 | 6 |
| Gnome | 4 | 2 | 2 |
| Troll | 7 | 3 | 4 |
| Blood Elf | 8 | 4 | 4 |
| Draenei | 2 | 1 | 1 |

| Class | Total | Control | Assist-only |
| --- | ---: | ---: | ---: |
| Warrior | 14 | 7 | 7 |
| Paladin | 9 | 5 | 4 |
| Hunter | 10 | 5 | 5 |
| Rogue | 6 | 3 | 3 |
| Priest | 7 | 3 | 4 |
| Shaman | 2 | 1 | 1 |
| Mage | 10 | 5 | 5 |
| Warlock | 10 | 5 | 5 |
| Druid | 12 | 6 | 6 |

## Treatment contract

Assist-only may:

- pick up a quest within 200 yards;
- turn in a completed quest within 250 yards;
- intervene only on null, exploration, or already inactive/expired targets.

Assist-only may not:

- route to quest objectives;
- replace an active grind target;
- replace an RPG target or any active `new rpg` state;
- acquire or retain an intent while loot is available;
- remove the `new rpg` strategy.

An assist intent has a one-minute total lease. Lack of movement or approach for 30 seconds releases
the intent and applies a two-minute quest cooldown.

## Measurement contract

All aggregate lines contain the run ID. In addition to the existing flight recorder, this run saves:

- intent acquisition, semantic progress, repoints, preemptions, endings, distance, duration, and
  previous target category by arm and intent type;
- sampled player level, populated-slot average item level, total equipped item-level points,
  occupied slots, and weapon item level by arm;
- individual loot items, equippable loot items, equippable quest-reward items, equip events, and
  their item-level sums.

Durable `strict_altbot_levelups` rows remain the milestone source for played time, quest count, and
item level at each attained level.

## Promotion gates

The headline comparison should use fixed exposure and level bands rather than raw quest count.

- total XP per bot-hour no worse than control by more than 2% for this component test;
- no late level band worse by more than 5%;
- loot per kill not reduced;
- travel share increase below approximately 3 percentage points;
- same-level equipment totals, occupied slots, weapon item level, and populated-slot item level not
  worse;
- no class-specific catastrophic tail and effectively zero hard stalls.

The final feature goal remains a reproducible improvement of at least 5% in median time to level,
with equipment progression improved or preserved across more than one cohort seed.

## Launch verification

The first complete recorder interval contained exactly 40 control and 40 assist-only bots. The
assist arm acquired four local pickup intents at an average 3.5 yards (maximum 4 yards): three
succeeded and one yielded to protected activity. There were no exhausted intents or hard stalls.
Both flight and gear lines reported 40 bots and 440 equipment samples per arm.

These first-minute values are a smoke test only and must not be interpreted as an outcome.

## Operational note

After both guilds were created, the temporary setup world process entered a CPU-bound world-thread
spin and could not execute its queued SOAP shutdown. Its exact executable and PID were verified,
the already-durable cohort/guild records were checked in the database, and only that process was
stopped. The final startup reset online state normally and brought all 80 bots online.
