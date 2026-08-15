#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"

#include <unordered_map>

#include "LazyQuestSelector.h"

namespace
{
    constexpr uint32 CHECK_INTERVAL_MS = 3 * IN_MILLISECONDS;

    struct LazyBotState
    {
        uint32 lastCheckMs = 0;
        uint32 lastQuestId = 0;
        TravelDestination* lastDestination = nullptr;
        WorldPosition* lastPoint = nullptr;
    };

    std::unordered_map<uint64, LazyBotState> botStates;

    bool ShouldCheck(LazyBotState& state, uint32 now)
    {
        if (state.lastCheckMs != 0 && getMSTimeDiff(state.lastCheckMs, now) < CHECK_INTERVAL_MS)
            return false;

        state.lastCheckMs = now;
        return true;
    }

    void LogCandidate(Player* bot, LazyBotState& state, LazyQuestCandidate const& candidate)
    {
        if (state.lastQuestId == candidate.questId && state.lastDestination == candidate.destination &&
            state.lastPoint == candidate.point)
            return;

        state.lastQuestId = candidate.questId;
        state.lastDestination = candidate.destination;
        state.lastPoint = candidate.point;

        Quest const* quest = candidate.destination->GetQuestTemplate();
        LOG_INFO("playerbots", "[LQ] {} would prefer quest {} [{}] -> {} at {:.0f}y", bot->GetName(),
                 candidate.questId, quest ? quest->GetTitle() : "<unknown>", candidate.destination->getTitle(),
                 candidate.distance);
    }
}

class LazyQuestingPlayerScript final : public PlayerScript
{
public:
    LazyQuestingPlayerScript()
        : PlayerScript("LazyQuestingPlayerScript", { PLAYERHOOK_ON_AFTER_UPDATE })
    {
    }

    void OnPlayerAfterUpdate(Player* player, uint32 /*p_time*/) override
    {
        if (!player || !player->IsInWorld() || !player->IsAlive() || player->IsInCombat() ||
            player->IsBeingTeleported())
            return;

        if (!GET_PLAYERBOT_AI(player))
            return;

        LazyBotState& state = botStates[player->GetGUID().GetRawValue()];
        if (!ShouldCheck(state, getMSTime()))
            return;

        LazyQuestCandidate candidate;
        if (FindLazyQuestCandidate(player, candidate))
            LogCandidate(player, state, candidate);
        else
        {
            state.lastQuestId = 0;
            state.lastDestination = nullptr;
            state.lastPoint = nullptr;
        }
    }
};

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
    new LazyQuestingPlayerScript();
}
