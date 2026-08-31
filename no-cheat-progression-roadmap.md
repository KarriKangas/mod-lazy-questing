# Strict Altbot no-cheat progression roadmap

**Status:** proposed experimental program; no treatment is approved for production

**Goal:** make strict Altbots reach levels at least 5% faster, with 10% as the stretch target, while
improving equal-level equipped item level and preserving normal World of Warcraft mechanics

**Baseline:** Cohorts 8-11; Cohort 12 is invalid and permanently excluded

**Exposure:** 25,200 played seconds per bot for full representative cohorts

## 1. Executive decision

The next program will optimize decisions and reliability, not character power.

The strongest route to faster leveling is to increase productive combat cycles per played hour:
select reachable targets, abandon targets that make no progress, shorten genuinely unproductive
decision gaps, and favor targets with better observed XP per minute. Quest interactions may add
value only when they require no independent route. Looting, equipment decisions, and service trips
must preserve or improve gear without introducing free items or resources.

No future treatment may alter damage, health, regeneration, movement speed, XP, loot generation,
equipment generation, money, cooldowns, or combat rules. Cohort 12 attempted a PvE damage
multiplier, violated this constraint, was stopped after one startup interval, and is not evidence.

The likely headline progression gains are Cohorts 16 and 17. Cohorts 13-15, 18-20 are supporting
components that either add zero-detour rewards, protect loot and gear, or remove catastrophic tails.
Only independently passing components may enter the composite treatment.

## 2. Evidence from completed cohorts

### Cohort 8: broad quest routing

Full Lazy Questing activated but reduced XP per bot-hour by 17.9%. Travel increased by 19.3
percentage points while fight share, kills, loot, quests, and money fell. The treatment created work,
but the work displaced more productive combat and became worse late in the run.

### Cohort 9: assist-only routing

Assist-only improved quest XP by 3.5%, completions by 4.2%, and turn-ins by 9.1%. Total XP still
fell 6.5%, kills fell 5.4%, and loot fell 14.0%. Gear was lower despite similar occupied-slot
coverage. More quest activity was not enough to compensate for reduced combat and loot throughput.

### Cohort 10: conservative local assistance

Median time to level 9 improved only 0.85%. Eleven intents produced one success; most were
preempted by protected activity. XP stayed near control, but equippable loot, equip events, and
equal-level gear regressed. The remaining cost was intent arbitration and churn, not excess travel.

### Cohort 11: atomic in-range assistance

The treatment could not acquire travel ownership and structurally protected combat, loot, and RPG
activity. It nevertheless performed only two successful interactions in the complete run. The
level-9 median was 3.80% faster, but a nearly dormant treatment cannot explain that difference.
Late-band, gear-quality, stall, and tail gates also failed.

A later activation-only diagnostic found that the general RPG guard suppressed most atomic scans.
When two legal in-range candidates were attempted, both succeeded. The interaction works; the
opportunity contract and scheduling are the bottleneck.

### Cross-cohort diagnosis

1. Combat is the dominant source of leveling progress in the observed level bands.
2. Travel and intent ownership are expensive unless they quickly produce semantic progress.
3. Loot throughput and kill cadence are leading indicators for equal-level gear quality.
4. Broad idle time is not automatically waste: eating, drinking, looting, servicing, and waiting
   for safe combat are legitimate. Only explicitly eligible decision gaps should be recovered.
5. Unreachable targets, slow target recovery, and repeated failed service trips create severe tails.
6. Atomic quest actions are safe only when they piggyback on work the bot is already doing.

## 3. No-cheat contract

### Allowed behavior changes

- Choose among visible, legal, XP-granting targets.
- Use existing navmesh/path results to reject unreachable or extremely circuitous targets.
- Abandon a target after bounded lack of observable progress and select another legal target.
- Invoke the existing Playerbots decision cycle sooner during a verified eligible decision gap.
- Learn target efficiency from the bot's observed combat results.
- Interact with an NPC or game object only at normal interaction distance.
- Loot a legitimately killed, reachable corpse through the normal loot system.
- Buy ammunition, repair, sell, train, and buy useful items using normal NPCs and the bot's money.
- Equip legitimately obtained items when the class/spec equipment scorer considers them better.

### Forbidden mechanics

- Stat, damage, healing, mitigation, resource, or regeneration bonuses.
- Direct or multiplied XP, quest credit, money, reputation, or skill gains.
- Generated items, ammunition, equipment, rewards, or altered loot tables/drop chances.
- Teleports, taxi cheating, noclip, movement-speed changes, or pathing bypasses.
- Modified spell coefficients, cooldowns, global cooldowns, attack speeds, or combat rules.
- Future-loot knowledge or other hidden information a player could not reasonably observe or learn.
- Manual bot recovery, live configuration changes, or server restarts during an efficacy cohort.

Every cohort needs a one-sentence treatment contract and a reviewed code/config diff before launch.
If a proposed implementation cannot be described as choosing or timing a normal player action, it
does not belong in this program.

## 4. Experimental architecture

### Implementation boundary

Keep cohort implementations in `mod-lazy-questing` so AzerothCore and `mod-playerbots` remain
unmodified. The module may observe Playerbots context and request ordinary Playerbots actions
through the interfaces already used by this module. If a proposed lever cannot be implemented
safely without changing a sibling repository, stop for a separate design review instead of
silently patching the dependency.

### Representative cohorts

- 80 fresh bots: 40 control and 40 treatment.
- Exactly faction-balanced and optimally race/class-balanced using the repository seed tool.
- Stable arm assignment reconstructed with the production experiment hash.
- Fixed endpoint of 25,200 played seconds since first login for every bot.
- One behavior change per component cohort.
- Unchanged control behavior and identical instrumentation in both arms.
- Exact run ID on all recorder records; split and preserve each server epoch independently.

The targeted hunter service experiment is the exception: it should use a hunter-only cohort that is
balanced across faction and eligible races, followed by validation inside a representative
composite cohort.

### Activation phase

The first 30 minutes are an activation and integrity audit, not an efficacy decision. Verify:

- expected arm counts and faction/race/class balance;
- zero treatment actions in control;
- treatment actions obey the exact distance, state, and ownership contract;
- no generated resources, stat changes, teleportation, or combat-rule changes;
- no startup/shutdown fragment included as a complete recorder interval;
- zero hard stalls, protected-state violations, and cancelled ownership leaks.

Stop an experiment early for contamination or a clearly inactive treatment. Do not stop it early
because the outcome estimate is merely unfavorable.

### Endpoint analysis

Use the process in `AGENTS.md` and start every report from
`experiment-logs/REPORT_TEMPLATE.md`. Compare:

- Kaplan-Meier played time to the latest well-sampled common level;
- XP, kill XP, quest XP, level-ups, kills, and deaths per bot-hour;
- first/last windows and same-level bands;
- travel, fight, loot, interact, service, death, and eligible-idle shares;
- loot per kill and equippable loot/reward throughput;
- equal-level populated-slot item level, total equipped item-level points, occupied slots, weapon
  item level/quality, and equip events;
- worst bots and class/race tails, with disclosed sensitivity analyses for catastrophic outliers.

## 5. Cohort sequence

The sequence is contingent. A failed component is documented and omitted from the composite; its
code is not carried forward merely because it is available.

### Cohort 13: same-target quest piggyback

**Change:** While the bot is already interacting with its current RPG NPC, allow one pickup or
turn-in at normal interaction distance when the quest relation matches that exact RPG target. Do
not acquire an intent, replace a target, change a route, or suppress combat, loot, or service work.

**Test:** Fresh representative 40/40 cohort. Atomic interactions are disabled by default and may be
enabled only in the treatment run configuration after review. Require at least ten successful
actions and at least 50% attempt conversion by the endpoint. If the first-hour rate cannot plausibly
reach the activation floor, stop as inactive rather than running seven hours.

**Analyze:** Scans and every rejection reason, target matches/mismatches, attempts, successes,
quest XP, pickups, turn-ins, reward gear, interaction time, travel, fight time, XP, and loot.

**Expected role:** Low-risk additive reward/gear component. It is unlikely to create a 5% overall
progression gain by itself.

### Cohort 14: reachable grind-target filtering

**Change:** Before accepting a visible grind target, reject candidates without a complete navmesh
path or with a preregistered extreme path/direct-distance ratio. Choose the next legal candidate.
Do not move the bot, create a target, or bypass pathing during validation.

**Test:** Compare against the unchanged nearest-valid-target policy. Apply only at target selection;
do not add a no-progress watchdog in this cohort.

**Analyze:** Candidate counts, path rejections, selection latency/CPU cost, target-acquisition time,
movement failures, fight share, kills/hour, XP/hour, deaths, and worst-bot tails.

**Expected role:** Prevent bad targets and vertical/geometry failures with little median downside.

### Cohort 15: bounded no-progress watchdog

**Change:** If target distance, combat state, position, and XP all show no meaningful progress for a
preregistered interval, abandon only that target, place it on a short local cooldown, and ask the
normal selector for another. Never teleport or move the bot directly.

**Test:** Run separately from Cohort 14 so recovery effects are identifiable. Freeze the progress
threshold and cooldown before launch using control traces, not treatment outcomes.

**Analyze:** Stall count and duration, time to recovery, repeated-target loops, target cooldowns,
XP/hour, time-to-level tails, deaths, and class/race outliers.

**Expected role:** Remove catastrophic tails and make the later efficiency treatment safer.

### Cohort 16: eligible-idle combat reacquisition

**Change:** When a bot is alive and truly free of combat, loot, food/drink, spell casts, service,
travel, valid RPG work, and resource recovery, expire stale/null state sooner and invoke the
existing normal target-selection cycle. Do not accelerate attacks or shorten game cooldowns.

**Test:** Instrument eligible decision-gap time in both arms. The treatment may act only after a
short preregistered gap and must yield immediately when any protected work appears.

**Analyze:** Eligible-idle seconds, latency from completed action to the next target/fight, fight
share, kills/hour, XP/hour, mana/health state at pull, deaths, loot, and late-band behavior.

**Expected role:** Primary candidate for a 3-8% progression gain and potentially the first standalone
5% win.

### Cohort 17: observable XP-per-minute target ranking

**Change:** Rank reachable visible candidates using level, health, distance, quest relevance, and
the bot's observed recent kill time and XP. Do not query future drops or grant combat knowledge.

**Test:** Ranking versus current nearest-valid-target selection, with the reachability policy held
constant in both arms. Keep a bounded per-bot or class/zone observation history so the treatment
does not gain global omniscience.

**Analyze:** Kill duration, XP per engaged minute, kills/hour, target-level distribution, travel,
deaths, loot/kill, quest credit, level bands, and class/race tails.

**Expected role:** Primary candidate for a 3-8% gain. Combined with eligible-idle recovery, this is
the most credible path to the 5-10% program target.

### Cohort 18: bounded post-kill loot commitment

**Change:** While out of combat, with bag space and a reachable valid corpse, finish normal looting
before selecting new travel/RPG work. Abandon the corpse after a bounded path/interaction failure.

**Test:** Add symmetric telemetry for corpse availability, acquisition, reach, open, store, and
abandonment reason. Do not alter loot contents or corpse ownership.

**Analyze:** Loot/kill, equippable loot/hour, money, corpse abandonment, time from kill to next
attack, XP/hour, travel, bag pressure, and equal-level gear.

**Expected role:** Preserve or improve gear while limiting the progression cost of looting.

### Cohort 19: legitimate-upgrade equipment decisions

**Change:** Improve prompt equipping of legitimately obtained upgrades. First shadow-log inventory
items rejected by the current score threshold. Then preregister a lower hysteresis only where the
class/spec item score improves, with weapon DPS, two-hand/offhand, armor proficiency, unique-equip,
and durability safeguards.

**Test:** Keep loot acquisition identical; change only the decision to equip an owned item. Never
equip an item solely to raise the experiment's raw item-level metric.

**Analyze:** Candidate upgrades, rejection reasons, acceptance, churn, equal-level populated-slot
item level, total points, occupied slots, weapon DPS/item level/quality, deaths, and XP.

**Expected role:** Convert legitimate inventory into useful equipped power and improve measured
gear without generating items.

### Cohort 20: hunter service-trip economics

**Change:** Start an ammunition trip only when it is operationally necessary and affordable. After
a failed trip, retry only when money, position, inventory, or vendor availability materially
changes instead of every 30 seconds. Ammunition must still be purchased normally.

**Test:** First use a faction/race-balanced hunter-only 40/40 cohort for statistical power. Validate
any passing policy later in a representative cohort.

**Analyze:** Ammo counts, service trips, failed trips, money at departure, travel/service share,
melee fallback, kills/hour, XP/hour, deaths, weapon/gear outcomes, and hunter-tail level times.

**Expected role:** Remove a known hunter-specific tail and repeated unproductive travel.

### Cohort 21: winners-only composite

**Change:** Combine only components that passed their activation, mechanism, progression/no-harm,
travel, loot, gear, stall, and tail gates. Record the exact included component revisions.

**Test:** Fresh representative 80-bot, 40/40 cohort at 25,200 played seconds with a new balanced
seed. No tuning after launch.

**Analyze:** Every primary and supporting endpoint, first/last windows, common level bands, equal-
level gear, and class/race tails. Report both intent-to-treat and any preregistered sensitivity.

**Expected role:** Demonstrate the complete 5-10% behavior-only benefit.

### Cohort 22: exact composite replication

**Change:** None. Repeat the identical Cohort 21 treatment and thresholds with a new balanced seed.

**Test and analyze:** Use the same exposure, metrics, exclusions, and gates. Do not revise the
treatment between seeds.

**Expected role:** Establish reproducibility. One favorable composite cohort is insufficient for
promotion.

## 6. Promotion gates

### Supporting component gate

A supporting component may continue only if:

- the intended mechanism activates at its preregistered minimum;
- XP and median time-to-level are no more than 2% worse than control;
- no late window or level band is more than 5% worse;
- travel rises by less than approximately 3 percentage points;
- loot, gear, deaths, stalls, and relevant class/race tails are preserved; and
- its specific mechanism improves materially enough to justify composite complexity.

Passing this gate does not make a component a production recommendation.

### Main throughput gate

Cohorts 16 and 17 should show at least a 5% improvement in either the latest well-sampled
time-to-level median or a consistent progression-rate endpoint, with the other progression metrics
directionally agreeing and all preservation gates passing. A smaller result may be informative but
is not the program goal.

### Composite promotion gate

Cohorts 21 and 22 must both show:

- at least 5% faster Kaplan-Meier median time to the latest well-sampled common level;
- positive XP/hour and level-up-rate evidence consistent with the time-to-level result;
- no late window or level band more than 5% worse;
- loot/kill and equippable loot/reward throughput no worse;
- a target of at least 3% better equal-level median populated-slot item level, with total equipped
  points, occupied slots, and weapon item level/quality no worse;
- travel below approximately +3 percentage points;
- effectively zero hard stalls and no catastrophic class/race tail; and
- intact assignment, exposure, recorder, and treatment-contract evidence.

If the two seeds disagree, do not average them into a promotion. Isolate the interaction or run a
predeclared third replication.

## 7. Implementation and review workflow

Before each cohort:

1. Write the one-sentence treatment contract and forbidden side effects.
2. Review the source/config diff with the owner before building or launching.
3. Add symmetric activation and mechanism counters for both arms where applicable.
4. Verify no damage, XP, loot-generation, movement, teleport, or item-generation hooks changed.
5. Build `worldserver` and record its SHA-256.
6. Generate a fresh balanced roster/seed and reserve new guild IDs.
7. Stage the exact live configuration while the server is stopped.
8. Launch only after explicit approval.

During and after each cohort, follow `AGENTS.md`. Never restart for convenience, modify a live
treatment, manually recover a bot, or mix recorder epochs. Preserve exact-run evidence before an
unavoidable restart and keep raw runtime logs out of Git.

## 8. Current repository state

The atomic activation counters and same-RPG-target piggyback implementation exist as experimental
code behind `LazyQuesting.Assist.AtomicQuestInteractions`. The option is disabled by default and
must remain disabled outside an explicitly reviewed Cohort 13 configuration. Its presence in the
repository is not approval to run it.

The live server configuration is operational state, not repository state. Before resuming
experimentation, independently verify that the worldserver is stopped or intentionally running and
that no stale run ID or treatment percentage remains active.
