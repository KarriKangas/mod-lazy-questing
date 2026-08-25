# mod-lazy-questing

An AzerothCore module for lazy questing behavior for active strict Altbots,
powered by [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
and mod-strict-altbot-guild.

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
- aggregate scheduler and selector metrics are logged once per minute.

Quest completion, level changes, and map changes wake the affected bot without
waiting for its polling interval. The module has no SQL or commands.
