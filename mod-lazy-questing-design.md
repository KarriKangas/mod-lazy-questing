# mod-lazy-questing

## Lazy design doc for making Playerbots slightly less stupid at questing

**Status:** deliberately rough, incremental, homebrew-only  
**Primary constraint:** do not modify `mod-playerbots` or AzerothCore source. Everything lives in a separate AzerothCore module.  
**Reference snapshot:** current upstream/default-branch code reviewed 2026-08-15. Pin the exact commits used by the server when implementation starts.

---

## 1. The idea in one paragraph

`mod-lazy-questing` is not a replacement quest AI.

It is a small corrective layer around the existing Playerbots quest/travel machinery. Playerbots already knows a surprising amount: active quest state, quest givers and takers, objective creatures/game objects, server spawn positions, travel destinations, and how to move toward a `TravelTarget`. The first version should simply notice when a bot has useful quest work but is doing something less useful, pick a nearby valid quest destination, and nudge the bot's existing `TravelTarget` toward it.

The intended personality of the module is:

> Playerbots does its normal thing. The module occasionally looks over its shoulder and says, "No, go do this quest instead."

If a nudge is not obviously useful, do nothing and let stock Playerbots continue.

---

## 2. Goals

- Make bots spend more time progressing quests they already accepted.
- Prefer nearby useful quest work over grinding, exploring, or idling.
- Prefer turning in completed quests when convenient.
- Eventually recover from obviously stalled quest targets.
- Eventually cover a few common quest-item interactions that stock Playerbots misses.
- Keep the implementation small enough that each useful behavior can be added in a separate commit and watched in-game immediately.
- Keep `mod-playerbots` and AzerothCore repositories pristine.

## 3. Non-goals

For now, absolutely do not build:

- a general quest planner;
- an optimized 1-80 leveling route;
- a Questie runtime Lua engine;
- a database schema for every quest mechanic;
- a generic behavior tree;
- group-level route optimization;
- full scripted quest support;
- perfect support for every race/class/faction;
- a stable public Playerbots extension API;
- a big test framework.

This is single-player homebrew code. A small obvious hack that improves the aquarium is preferable to a beautiful framework that takes a week before a bot behaves differently.

---

## 4. Basic constraints / assumptions

1. The server already runs the Playerbots-compatible AzerothCore fork required by `mod-playerbots`.
2. `mod-playerbots` is present and enabled at build time.
3. `mod-lazy-questing` may include Playerbots headers and compile against Playerbots internals/public classes.
4. Upstream Playerbots changes are allowed to break this module at compile time. Fixing an include or method name after an upstream update is acceptable.
5. No source file in AzerothCore or `mod-playerbots` is edited by this module.
6. If the module cannot make a confident improvement, it should fail open and let Playerbots behave normally.

This is the same general tradeoff demonstrated by `mod-dungeon-clear`: keep upstream untouched, accept compile-time coupling to Playerbots.

---

## 5. The useful stuff Playerbots already gives us

The most important source file is:

`mod-playerbots/src/Mgr/Travel/TravelMgr.h`

It already exposes the pieces needed for a very small external quest nudger:

- `TravelMgr::getQuestTravelDestinations(...)`
- `TravelDestination::isActive(Player*)`
- `TravelDestination::nextPoint(...)`
- `TravelDestination::distanceTo(...)`
- `TravelDestination::getName()`
- `TravelDestination::getEntry()`
- `QuestTravelDestination::GetQuestTemplate()`
- `QuestObjectiveTravelDestination`
- `QuestRelationTravelDestination`
- `TravelTarget::setTarget(...)`
- `TravelTarget::setStatus(...)`
- `TravelTarget::getDestination()`
- `TravelTarget::isActive()` / `isWorking()` / `isTraveling()`
- `TravelTarget::isForced()`

That means V0 does not need to discover NPCs or parse quest text. Playerbots has already converted AzerothCore's quest/world data into travel destinations.

Also inspect:

`mod-playerbots/src/Ai/Base/Actions/ChooseTravelTargetAction.cpp`

This is the current stock decision maker. It is useful as documentation of what Playerbots considers a quest/RPG/grind/explore target, but we do **not** need to patch it.

The current code is also the reason this module is worth trying: the ordinary `SetQuestTarget()` path builds quest destinations but currently calls `getBestDestination()` with an empty `activePoints` vector, while `getBestDestination()` rejects empty points. Rather than carrying a Playerbots patch, this module can simply make the missing decision externally.

---

## 6. Proposed module shape

Start tiny:

```text
modules/
└── mod-lazy-questing/
    ├── CMakeLists.txt
    ├── conf/
    │   └── mod_lazy_questing.conf.dist       # optional; can wait
    └── src/
        ├── mod_lazy_questing_loader.cpp
        ├── LazyQuestingModule.cpp
        ├── LazyQuestSelector.h
        └── LazyQuestSelector.cpp
```

Do not create Strategy/Action/Trigger/Value directories yet.

Those only become useful if direct travel-target nudging eventually becomes awkward.

### Responsibilities

**LazyQuestingModule.cpp**

- AzerothCore script hook.
- Detect whether a `Player*` is actually controlled by Playerbots.
- Throttle evaluation; e.g. once every 2-5 seconds per bot.
- Apply boring safety gates: alive, in world, not teleporting, not in combat, etc.
- Read the bot's current `TravelTarget`.
- Ask `LazyQuestSelector` whether there is a better quest target.
- Apply the nudge if appropriate.

**LazyQuestSelector**

- No timers and no AzerothCore script lifecycle.
- Given `Player*`, inspect active/completed quests and Playerbots travel destinations.
- Return the best `(TravelDestination*, WorldPosition*)` candidate, or nothing.
- Start with nearest-distance selection. No clever scoring framework.

---

## 7. Where to hook it

Look first at:

`mod-playerbots/src/Script/Playerbots.cpp`

The Playerbots module itself obtains the bot AI and drives it from a PlayerScript update hook. Use the hook that already exists in **the exact Playerbots-compatible AzerothCore checkout used by the server**. Do not add a new core hook.

Current `mod-playerbots` uses `PLAYERHOOK_ON_AFTER_UPDATE` / `OnPlayerAfterUpdate` in its compatible core. Stock AzerothCore master may expose a slightly different update-hook set, which is precisely why the local Playerbots-compatible core is the authority here.

The module does not need delicate per-frame behavior. If its callback happens just before Playerbots, the new active travel target should be available to Playerbots on that update. If it happens after Playerbots, it will be available on the next one. Since evaluation is throttled to seconds, either is fine for V0.

### Getting the AI

Copy the boring proven pattern from Playerbots / `mod-dungeon-clear`, e.g. use the existing `GET_PLAYERBOT_AI(player)` or the equivalent manager lookup available in the local checkout.

Then obtain the AI object context and current travel target using the same value Playerbots itself uses:

```cpp
TravelTarget* current =
    ai->GetAiObjectContext()
      ->GetValue<TravelTarget*>("travel target")
      ->Get();
```

Do not create a second travel system.

---

# 8. Implementation chunks

The point of these chunks is that a coding agent can implement exactly one, then the server can be rebuilt and watched before deciding whether the next chunk is worth doing.

---

## Chunk 0 - Empty module that proves the dependency works

### Do

Create `mod-lazy-questing` as an AzerothCore module that includes Playerbots headers and logs one startup line.

It should compile only when `mod-playerbots` is present.

### Inspiration

- AzerothCore: **Create a Module** documentation and skeleton module.
- `mod-dungeon-clear/CMakeLists.txt`: explicitly documents that an external module can compile alongside and subclass/link against Playerbots.
- `mod-dungeon-clear/src/DungeonClearModule.cpp`: includes `Playerbots.h` and `PlayerbotAI.h` directly without modifying Playerbots.

### Don't do

- no config;
- no AI behavior;
- no context injection;
- no commands;
- no SQL.

### Stop and run

If worldserver starts and logs that the module loaded, commit it.

Suggested commit:

```text
Add mod-lazy-questing skeleton
```

---

## Chunk 1 - Find a nearby quest target, but do not change anything

This is deliberately read-only.

### Do

Every few seconds for a Playerbot that is out of combat:

1. Iterate `bot->getQuestStatusMap()`.
2. Ignore rewarded/irrelevant entries.
3. For each quest already in the bot's log, call `TravelMgr::instance().getQuestTravelDestinations(...)` for that quest.
4. Keep destinations where `dest->isActive(bot)` is true.
5. Ask each destination for a usable point using `nextPoint(&botPosition)`.
6. Pick the nearest candidate.
7. Optionally log something like:

```text
LazyQuest: Foo would prefer [Quest Name] -> objective Creature 123 at 417y
```

Do **not** include new quest pickup yet. The module's first problem is "bots accept quests and then ignore them," not "find every quest in the world."

### Candidate priority

Keep this stupid:

1. completed quest turn-in;
2. active quest objective;
3. distance.

If identifying turn-in vs objective cleanly is annoying, just choose the nearest active quest destination for the first pass. `isActive(bot)` already contains substantial quest-state filtering.

### Inspiration

- `ChooseTravelTargetAction::SetQuestTarget()` for how Playerbots loops quest state and asks `TravelMgr` for destinations.
- `TravelMgr.h` for `QuestRelationTravelDestination`, `QuestObjectiveTravelDestination`, `isActive`, `nextPoint`, and distance helpers.

### Stop and run

Watch logs for a handful of bots. Sanity check that the reported destinations correspond to quests actually in their logs and aren't on the other side of the planet.

Suggested commit:

```text
Find nearby active quest destinations
```

---

## Chunk 2 - The first actually useful behavior: nudge boring travel targets

Now let the module touch state.

### Do

If a good quest candidate exists, inspect the current `TravelTarget`.

Initially hijack only obviously low-value targets:

- no destination / inactive / expired target;
- `NullTravelDestination`;
- `GrindTravelDestination`;
- `ExploreTravelDestination`.

Initially leave these alone:

- any current quest destination;
- any forced target;
- group-copy targets;
- RPG targets;
- anything while in combat.

Then:

```cpp
current->setTarget(best.destination, best.point);
```

That is the core feature.

Do not build a custom movement action. Stock Playerbots should see the new `TravelTarget` and perform its normal travel/work lifecycle.

### Why leave RPG alone at first?

Because an RPG target can be repair/vendor/trainer behavior that the bot actually needs. We can become more aggressive later once we see what the bots do.

### Inspiration

- `TravelTarget` in `TravelMgr.h` describes the PREPARE -> TRAVEL -> WORK -> COOLDOWN -> EXPIRE lifecycle.
- `ChooseTravelTargetAction::setNewTarget()` shows the normal Playerbots bookkeeping around replacing targets. If stale RPG/pull state later causes trouble, copy only the tiny pieces of cleanup that matter into this module rather than subclassing everything immediately.

### Stop and run

This is the first aquarium experiment that matters.

Watch whether bots that would previously grind/explore instead begin traveling toward objectives already in their quest logs.

If this alone produces a visible improvement, stop adding architecture.

Suggested commit:

```text
Prefer active quests over idle grind and exploration
```

---

## Chunk 3 - Become slightly more rude about RPG wandering

Only do this if Chunk 2 still leaves bots spending silly amounts of time hanging around NPCs.

### Do

Allow a nearby quest to replace `RpgTravelDestination`, but only if the obvious maintenance reasons are absent.

Use the same Playerbots AI values already referenced by `ChooseTravelTargetAction.cpp` for things such as:

- `should sell`
- `can sell`
- `should repair`
- `can repair`

The rule can be crude:

```text
if current target is RPG
and bot does not urgently need sell/repair
and useful quest target is within MaxQuestDistance
    -> quest wins
```

Do not try to classify every RPG activity.

### Possible config, if we finally want one

```ini
LazyQuesting.Enable = 1
LazyQuesting.CheckIntervalMs = 3000
LazyQuesting.MaxQuestDistance = 2500
LazyQuesting.HijackRpg = 1
```

Hardcoded values are also acceptable until tuning becomes annoying.

Suggested commit:

```text
Prefer nearby quests over nonessential RPG travel
```

---

## Chunk 4 - Stop retrying one dead-end destination forever

This is the first bit of memory.

### Keep it primitive

Maintain a tiny per-bot record in the module:

```text
current quest id
current destination identity / entry / position
last observed quest progress fingerprint
last progress timestamp
```

The progress fingerprint can simply be derived from the quest's current item/creature counters or even the whole relevant `QuestStatusData` if convenient.

### Rule

If the bot has been working/traveling the same quest target for, say, 60-120 seconds and the relevant quest progress has not changed:

1. temporarily blacklist that destination/point in the module;
2. expire or replace the current `TravelTarget`;
3. run the normal lazy selector again;
4. let Playerbots try another destination.

Blacklist lifetime can be a dumb 2-5 minutes.

No generalized watchdog framework.

No persistent database.

No fancy failure taxonomy.

### Inspiration

- `TravelTarget` already tracks status, retries, and expiry; reuse those concepts instead of creating a second state machine.
- `TravelMgr::getObjectiveStatus()` and quest status data show how Playerbots determines whether an objective still needs progress.

Suggested commit:

```text
Move on from stalled quest destinations
```

---

## Chunk 5 - Stop throwing useful quests away while experimenting

This may not even require module code.

Inspect:

- `mod-playerbots/src/Ai/Base/Actions/DropQuestAction.cpp`
- `mod-playerbots/conf/playerbots.conf.dist` (`DropObsoleteQuests`)

Current `CleanQuestLogAction` drops grey/trivial quests when the corresponding config is enabled. For this homebrew leveling experiment, consider disabling that cleanup while evaluating quest behavior:

```ini
AiPlayerbot.DropObsoleteQuests = 0
```

This is a server configuration choice, not a Playerbots source modification.

If quest-log pressure later becomes a real problem, add a small module policy for which quests may be discarded. Do not solve that before it is observed.

---

## Chunk 6 - Common quest-item-on-creature behavior

Only after normal kill/loot/objective travel looks better.

Stock `UseItemAction` is useful inspiration because its protected `UseItem(...)` implementation already knows how to emit item-use packets and accepts an optional `Unit* unitTarget`.

Look at:

- `mod-playerbots/src/Ai/Base/Actions/UseItemAction.h`
- `mod-playerbots/src/Ai/Base/Actions/UseItemAction.cpp`

The module can subclass `UseItemAction` purely as a helper and expose one small public wrapper, without modifying Playerbots:

```cpp
class LazyQuestItemUse : public UseItemAction
{
public:
    explicit LazyQuestItemUse(PlayerbotAI* ai)
        : UseItemAction(ai, "lazy quest item use", true) {}

    bool UseOnUnit(Item* item, Unit* target)
    {
        return UseItem(item, ObjectGuid::Empty, nullptr, target);
    }
};
```

Then implement only the obvious case:

```text
active quest has source/quest item
current objective clearly refers to creature X
creature X is nearby
    -> use item on creature X
```

If the necessary item-to-target relationship cannot be derived reliably from AzerothCore/Playerbots data, **that is the point where Questie hints become useful**.

Do not solve every item quest.

Suggested commit:

```text
Use obvious quest items on objective creatures
```

---

## Chunk 7 - Tiny Questie hint table, not Questie integration

This is intentionally much later than the original Questie idea.

When we have a concrete pile of quests where the bot knows where to go but cannot infer the interaction, add an offline-generated hint file.

Start with something as boring as:

```cpp
struct QuestHint
{
    uint32 questId;
    uint32 itemId;
    uint32 targetEntry;
};
```

or a compact generated JSON/CSV loaded by the module.

Example semantics:

```text
quest 9303
item 22962
use on creature 16534
```

Questie is then just one source used by an offline converter to populate hints.

Do **not** parse Questie's Lua tables in worldserver.

Do **not** override AzerothCore spawn locations with Questie coordinates when a real server entity exists.

Suggested commit:

```text
Add generated hints for unsupported quest-item objectives
```

---

# 9. Optional escape hatch: inject proper Playerbots actions later

Do not start here.

If the module eventually needs normal Playerbots trigger/strategy scheduling rather than periodic corrective nudges, copy the integration approach from `mod-dungeon-clear`.

Relevant files:

- `mod-dungeon-clear/src/DungeonClearModule.cpp`
- `mod-dungeon-clear/src/AiObjectContextAccess.h`
- `mod-dungeon-clear/src/DcStrategyGate.cpp`

The important pattern is:

1. subclass Playerbots `Strategy`, `Action`, `Trigger`, and/or `Value` types;
2. create module-owned context factories;
3. append them to every per-class Playerbots shared context registry after Playerbots has built its contexts;
4. optionally use the private-static access shim only for the base `AiObjectContext` registries;
5. install the strategy on bots with `ChangeStrategy(...)` and re-assert it after Playerbots resets strategies.

`mod-dungeon-clear` proves this can be done while keeping `mod-playerbots` untouched.

For `mod-lazy-questing`, this is V2 architecture, not V0 architecture.

Use it only when the direct script approach becomes painful.

---

# 10. The intended behavior ladder

The module should eventually feel roughly like this:

```text
bot is in combat / teleporting / dead / forced somewhere
    -> leave it alone

bot already has a useful quest travel target
    -> leave it alone

bot has a completed quest with a nearby turn-in
    -> nudge toward turn-in

bot has an active nearby quest objective
and is idle / grinding / exploring
    -> nudge toward quest objective

bot is doing nonessential RPG wandering
and a nearby quest is available
    -> maybe nudge toward quest

bot has made no progress on this objective for a long while
    -> temporarily avoid this target and try another

bot reaches an objective requiring an obvious item-on-creature interaction
    -> perform that interaction

module does not understand the quest
    -> do nothing; stock Playerbots continues
```

That is enough architecture for a long time.

---

# 11. A deliberately dumb scoring function

Do not create a generic utility framework.

If simple nearest-distance stops being enough, add a few constants directly to `LazyQuestSelector`:

```text
score = distance

completed turn-in:      score -= 1000
nearly complete quest:  score -= 300
recently stalled point: score += 100000
```

Lowest score wins.

If this gets beyond ~5-6 rules, reconsider it. Until then, a few `if` statements are easier to understand while watching bots in the aquarium.

---

# 12. Module state: keep it in RAM and forget it on restart

A tiny map keyed by bot GUID is enough:

```cpp
struct LazyBotState
{
    uint32 nextCheckMs;
    uint32 lastProgressMs;
    uint32 questId;
    uint32 progressHash;
    std::vector<TemporaryBlacklistEntry> blacklist;
};
```

No SQL.

No migrations.

No persistence.

Server restart wipes the module's memory. That is fine.

---

# 13. Logging

Logging is more useful than tests for the first iterations because the desired result is behavioral.

Keep one optional debug category/message shape:

```text
LQ Foo: grind -> quest 1234 objective 2, 386y
LQ Bar: quest 5678 stalled 90s, blacklisting creature 123 for 180s
LQ Baz: used item 22962 on creature 16534 for quest 9303
```

Do not log every update tick.

The useful question is always: **why did the module interfere?**

---

# 14. Things to copy versus things not to copy

## Copy ideas from Playerbots

From `ChooseTravelTargetAction.cpp`:

- how quest status is iterated;
- which AI values represent sell/repair needs;
- how existing travel target categories are named;
- the small cleanup done when a travel target changes.

From `TravelMgr.h/.cpp`:

- quest destination discovery;
- objective activity checks;
- destination points;
- distance/path concepts;
- travel target lifecycle.

From `UseItemAction.h/.cpp`:

- actual item-use execution;
- targeting an existing Unit or GameObject.

From `DropQuestAction.cpp`:

- what Playerbots currently considers obsolete/trivial quest cleanup.

## Copy ideas from mod-dungeon-clear

From `CMakeLists.txt`:

- external module dependency/linkage shape against Playerbots.

From `DungeonClearModule.cpp`:

- getting `PlayerbotAI*` from AzerothCore script hooks;
- timing registration after Playerbots initialization;
- periodically reconciling behavior when Playerbots resets itself.

From `AiObjectContextAccess.h`:

- only if we later decide to inject our own action/strategy contexts.

## Do not copy yet

- dungeon-clear's large strategy/trigger graph;
- asynchronous path workers;
- test harnesses;
- custom event frameworks;
- complex settings systems;
- context injection just because it is clever.

`mod-dungeon-clear` is proof that external integration works, not a template for how large this module should become.

---

# 15. Suggested implementation order for a coding agent

Give the coding agent one item at a time.

### Prompt 1

> Create a minimal AzerothCore `mod-lazy-questing` module that depends on the existing `mod-playerbots`, includes Playerbots headers, registers one script, and logs successful startup. Do not modify AzerothCore or Playerbots. Do not add any quest behavior yet. Use `mod-dungeon-clear` only as build/integration inspiration.

### Prompt 2

> Add a read-only `LazyQuestSelector` that, for an existing Playerbot, scans quests already in its quest log using Playerbots `TravelMgr::getQuestTravelDestinations`, filters active destinations, obtains candidate points, and returns/logs the nearest useful quest destination. Do not change the bot's travel target yet. Keep the implementation simple.

### Prompt 3

> Use `LazyQuestSelector` from the module's throttled PlayerScript update. If the current Playerbots travel destination is null, grind, explore, inactive, or expired, replace it with the selected quest destination using the existing `TravelTarget`. Do not override forced targets, current quest targets, RPG targets, combat behavior, or group behavior.

### Prompt 4

> Extend the lazy quest nudge so a nearby quest can replace an RPG destination only when Playerbots does not currently need to sell or repair. Reuse the existing AI values visible in `ChooseTravelTargetAction.cpp`. Keep all other behavior unchanged.

### Prompt 5

> Add tiny in-memory stall detection for quest destinations. If the same quest target produces no quest progress for roughly 90 seconds, blacklist that target for a few minutes, expire/reselect the travel target, and let the existing lazy selector choose another candidate. No persistence or generic watchdog framework.

### Prompt 6

> Add one narrow helper for obvious quest-item-on-creature interactions by subclassing/reusing Playerbots `UseItemAction` rather than reimplementing item packets. Do not add Questie yet. Only handle cases where the target creature and item relationship can be determined confidently from existing server/Playerbots data.

That is probably several evenings of useful changes without ever touching upstream source.

---

# 16. Reference map

These are the first places to open while implementing.

## Playerbots

- Current travel chooser:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Ai/Base/Actions/ChooseTravelTargetAction.cpp
- Travel chooser interface / useful protected helpers if we later subclass it:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Ai/Base/Actions/ChooseTravelTargetAction.h
- Travel destinations and `TravelTarget`:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Mgr/Travel/TravelMgr.h
- Travel destination implementation:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Mgr/Travel/TravelMgr.cpp
- Playerbots AzerothCore script/update integration:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Script/Playerbots.cpp
- Item-use plumbing:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Ai/Base/Actions/UseItemAction.h  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Ai/Base/Actions/UseItemAction.cpp
- Quest cleanup / abandonment behavior:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/src/Ai/Base/Actions/DropQuestAction.cpp
- Playerbots config:  
  https://github.com/mod-playerbots/mod-playerbots/blob/master/conf/playerbots.conf.dist

## External Playerbots module proof: mod-dungeon-clear

- Build/module setup:  
  https://github.com/jrad7/mod-dungeon-clear/blob/master/CMakeLists.txt
- Runtime integration and context registration:  
  https://github.com/jrad7/mod-dungeon-clear/blob/master/src/DungeonClearModule.cpp
- Zero-Playerbots-edit private registry access shim:  
  https://github.com/jrad7/mod-dungeon-clear/blob/master/src/AiObjectContextAccess.h
- Strategy reconciliation pattern:  
  https://github.com/jrad7/mod-dungeon-clear/blob/master/src/DcStrategyGate.cpp

## AzerothCore module basics

- Create a module:  
  https://www.azerothcore.org/wiki/create-a-module
- Modular structure / hooks:  
  https://www.azerothcore.org/wiki/the-modular-structure
- Module template:  
  https://github.com/azerothcore/skeleton-module

---

# 17. Final rule

Every time the module starts growing, ask:

> Can we get the same visible improvement by changing 20 lines and abusing an existing Playerbots object?

If yes, do that.

The first useful version of `mod-lazy-questing` should be embarrassingly small. Its job is not to understand World of Warcraft quests. Its job is to make the existing Playerbots quest knowledge win more often than random grinding, exploring, and wandering.

Only add real quest intelligence when the aquarium shows us a concrete failure that the next small commit can fix.
