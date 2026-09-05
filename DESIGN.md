# Character controller design

Status: initial design, not an implemented capability.

## Intent

Build a persistent character that pursues understandable goals, supports itself through normal gameplay, cooperates with others, and recovers from ordinary failures without administrator intervention.

Human-like means coherent commitments, limited knowledge, normal resource constraints, and individual preferences. It does not require artificial mistakes, maximum XP per hour, or pretending to human players that a bot is human.

## Rules of the world

- Use normal movement, pathing, interaction range, combat rules, cooldowns, costs, eligibility, and rewards.
- Never grant power, XP, money, items, ammunition, quest credit, or profession progress outside ordinary gameplay.
- Never use teleportation as travel or recovery. Transport mechanics must be explicitly classified before implementation; no administrator relocation or path bypass is allowed.
- Know only permitted static guide information, locally observable state, personal history, and information legitimately communicated to the character. Static quest-region knowledge does not grant hidden live spawn or auction knowledge.
- Audit inherited Playerbots and sibling-module behavior as well as new code. A strict-bot flag is not sufficient proof of compliance.

## Architecture

1. **Knowledge and memory:** bounded observations, learned failures, known services, and persistent commitments. Record the origin of knowledge.
2. **Goals:** select adventuring, maintenance, profession, social, or group goals using needs and stable preferences.
3. **Plans:** explicit, bounded sequences with prerequisites, completion conditions, failure reasons, and retry limits. Start with hand-authored plans for supported mechanics.
4. **Execution:** adapters request ordinary Playerbots actions and report actual outcomes. Request acceptance is not action completion.
5. **Arbitration and recovery:** one owner for each conflicting activity, explicit interrupt priorities, and deliberate resume or cancellation. Combat and urgent survival can suspend a plan. Independent controllers must not repeatedly overwrite each other's targets.

Reuse existing movement, combat, and interaction implementations where they satisfy these contracts. Do not recreate a game engine or build a universal planner first. Prefer a small reviewed Playerbots integration change over layers of competing target overrides when necessary; dependency edits require a concrete design and scoped review.

Example plan: accept suitable local quests, complete compatible objectives, loot, return to town, turn in, sell, repair, and train if affordable. Every step checks its own conditions and has bounded recovery.

## First milestone: one complete adventuring loop

Implement one starter area and a documented subset of quest mechanics. Start with a small roster covering several classes, including a supply-sensitive hunter.

The character must accept quests, reach and complete supported objectives, fight, loot, manage inventory, equip legitimate upgrades, purchase necessary supplies, turn in, and select its next activity. Include ordinary death recovery, unavailable objectives, unaffordable service trips, and unreachable targets.

Before implementing the loop:

- Trace the actual active Playerbots decision path and identify a single integration/ownership boundary.
- Audit legal execution and inherited convenience behavior for the selected scenario.
- Define observable action results and record goal changes, interruption reasons, failures, retries, and resumption.
- Define persistence behavior across logout/restart; revalidate transient targets instead of persisting pointers or assuming an old action still owns control.

Acceptance requires repeated whole-loop completion without manual rescue, no forbidden actions, bounded retries, and coherent resumption after interruptions. Set numeric scenario thresholds before running acceptance trials. Measure progression, deaths, resources, stalls, recovery time, and maintenance failures; do not promote a behavior merely because XP/hour rises.

## Expansion

After the first loop is reliable, validate another starting area and a broader roster. Extend the same contracts to group questing and dungeons, professions and commerce, and PvP. Add persistent preferences and relationships through observable behavior. Do not begin all systems at once.

## Evaluation

Use short scenario trials to prove activation and diagnose failures before long comparative cohorts. Keep bounded per-character decision traces outside Git and aggregate operational telemetry symmetrically. Freeze run identity, revisions, configuration, roster, exclusions, and endpoints for outcome experiments; separate process epochs and preserve evidence before restarts.

Evaluate legality, autonomy, coherent behavior, recovery, activity coverage, progression, and operational cost. Shared-world bots are not independent samples. Compare supported scenarios and class/race tails; a broad average must not hide characters that cannot sustain themselves.

The prior cohort sequence is historical evidence, not the current roadmap. No replacement controller or live experiment is authorized merely by this design document.
