# mod-lazy-questing

An AzerothCore module for lazy questing behavior powered by
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

The module includes Playerbots headers and logs `mod-lazy-questing loaded.`
when the worldserver starts. It periodically finds the nearest active quest
destination for out-of-combat Playerbots and can replace idle, grinding, or
exploring travel targets with that quest destination.

## Requirements

- AzerothCore
- mod-playerbots checked out alongside this module

This module intentionally has no configuration, context injection, commands,
or SQL.
