# mod-lazy-questing extends mod-playerbots and mod-strict-altbot-guild and is
# not a standalone module.
# AzerothCore auto-collects sources under src/. Those sources deliberately
# include headers from both modules, so the build succeeds only when both are
# present alongside this module.
# Keep this file touched when adding a source: this checkout's legacy source
# glob does not use CMake CONFIGURE_DEPENDS.
# Scheduler defaults are installed from conf/mod_lazy_questing.conf.dist by the
# AzerothCore module configuration collector.
