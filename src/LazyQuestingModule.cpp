#include "Log.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"

class LazyQuestingWorldScript final : public WorldScript
{
public:
    LazyQuestingWorldScript()
        : WorldScript("LazyQuestingWorldScript", { WORLDHOOK_ON_STARTUP })
    {
    }

    void OnStartup() override
    {
        LOG_INFO("server.loading", "mod-lazy-questing loaded.");
    }
};

void Addmod_lazy_questingScripts()
{
    new LazyQuestingWorldScript();
}
