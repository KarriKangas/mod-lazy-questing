# mod-lazy-questing extends mod-playerbots and is not a standalone module.
# AzerothCore auto-collects sources under src/. Those sources deliberately
# include Playerbots headers, so the build succeeds only when mod-playerbots is
# present alongside this module.
# Keep this file touched when adding a source: this checkout's legacy source
# glob does not use CMake CONFIGURE_DEPENDS.
