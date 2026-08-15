# mod-lazy-questing

An AzerothCore module for lazy questing behavior powered by
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

The current skeleton only proves the module dependency and startup lifecycle:
it includes Playerbots headers and logs `mod-lazy-questing loaded.` when the
worldserver starts.

## Requirements

- AzerothCore
- mod-playerbots checked out alongside this module

This module intentionally has no configuration, AI behavior, context
injection, commands, or SQL yet.
