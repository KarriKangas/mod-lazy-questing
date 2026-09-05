# Human-like WoW characters

A fresh start: build persistent bots that support themselves and pursue understandable goals through normal World of Warcraft gameplay.

Characters should quest, grind, explore, join groups and dungeons, participate in PvP, develop professions, trade, and interact with players. Faster leveling alone is not the product goal.

## Current state

Design and inert module skeleton only. No new character controller is implemented or enabled. The folder and loader retain the `mod-lazy-questing` name for build compatibility; the old quest nudger has been removed from active source.

Read [DESIGN.md](DESIGN.md) for the architecture and first milestone, and [AGENTS.md](AGENTS.md) for development rules.

The [accelerated bot laboratory](sandbox/README.md) provides an isolated headless test server at `D:\wowbot-lab`, with separate databases, launchers, and a simulation clock. It exercises strict Playerbots while the new controller is being developed.

## Previous project

The previous implementation, configuration defaults, reports, instructions, and uncommitted work were preserved in [the legacy snapshot](archive/lazy-questing-2026-09-05.zip). See [archive/README.md](archive/README.md) for verification and recovery. Git history is retained.

This repository reset does not update an already-built or running worldserver. A subsequent successful build and restart is required to remove the old behavior from that server. Old live configuration keys have no effect in the new inert module.
