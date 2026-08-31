# mod-lazy-questing

An AzerothCore module for lazy questing behavior for active strict Altbots,
powered by [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
and mod-strict-altbot-guild.

The evidence-based forward experiment program is documented in
[no-cheat-progression-roadmap.md](no-cheat-progression-roadmap.md). It treats normal game mechanics
as a hard boundary: future improvements may change bot decisions and timing, but never character
power, XP, generated loot, movement rules, or other gameplay rewards.

The module periodically finds the nearest active quest destination for
out-of-combat Playerbots registered in the mod-strict-altbot-guild roster and
can replace idle, grinding, exploring, or nonessential RPG travel with focused
quest work. Normal Playerbots are never registered with the lazy-questing
scheduler. It supports nearby quest pickup, objective travel, turn-in, and
direct interaction with quest givers.

Pickup destinations are indexed once by map and spatial cell. Bot work is
staggered through a central scheduler with a strict per-world-tick time budget,
so discovery does not scan every quest giver for every bot or block SOAP/CLI
processing. Candidate eligibility uses direct quest dialog checks instead of
creating permanent candidate-specific Playerbots AI values.

Login registration is retried with bounded exponential backoff while the
Playerbots AI is still being constructed. A periodic reconciliation against
the strict Altbot roster recovers missed login hooks without scanning every
online player.

Quest intent and the current travel leg are tracked separately. Combat, taxi
travel, teleports, group travel, and essential vendor or repair activity do not
count as quest stalls. When eligible work really stops moving or progressing,
the scheduler tries another unvisited spawn, objective source, or quest giver
before applying a bounded quest cooldown. Objective points are selected by
distance and existing visitors, which spreads a large cohort across available
spawns instead of concentrating it at a random point.

Strict Altbots can be assigned deterministically to three experiment modes:

- `control`, which is never touched by Lazy Questing;
- `assist-only`, a component-test arm. Atomic quest interactions can scan only within interaction
  distance for an immediate pickup or turn-in without acquiring an intent or changing the travel
  target;
- `current`, which retains the module's complete current behavior.

The configured control and assist percentages use a stable hash of character
GUID, race, class, and experiment seed. The unassigned remainder uses current
behavior, so the default zero-percent experiment configuration is backward
compatible.

An optional flight recorder observes all online strict Altbots, including the
control cohort. Exact XP events are split into kill, quest, exploration, and
other sources. Hook-driven counters retain kills, deaths, loot events, quest
pickups, turn-ins, and level-ups. Bounded per-bot snapshots retain objective
deltas, quest-completion transitions, and the current Lazy Questing intent. A
five-second sample attributes time to travel, combat, looting, interaction,
service, death/corpse-running, or idle activity. Once per minute these
measurements are emitted as one aggregate log line per experiment mode; no
per-event or per-bot log stream is produced.

Each experiment has a configured run identifier included in scheduler, intent, flight, and gear
lines. Intent aggregates include type, acquisition distance, previous target category, progress,
preemption, outcome, and duration. Gear aggregates include same-window character level, populated
slot item level, total equipped item-level points, occupied slots, weapon item level, loot gear,
quest-reward gear, and equip events.

## Requirements

- AzerothCore
- mod-playerbots checked out alongside this module
- mod-strict-altbot-guild checked out alongside this module

## Configuration

The defaults in `conf/mod_lazy_questing.conf.dist` are intended to remain safe
as bot counts grow:

- active intents are maintained every 5 seconds;
- idle discovery starts at 30 seconds and backs off to 2 minutes after misses;
- scheduler work is limited to 2 milliseconds per world tick;
- aggregate registration, progress, recovery, scheduler, and selector metrics
  are logged once per minute.
- experiment assignments remain stable across restarts and are included in the
  aggregate scheduler metrics.
- assist-only atomic quest interactions are disabled by default and can be enabled explicitly for
  a reviewed component test.
- flight-recorder activity is sampled every 5 seconds and its counters are
  aggregated once per minute by experiment mode.

Quest completion, level changes, and map changes wake the affected bot without
waiting for its polling interval. The module has no SQL or commands.
