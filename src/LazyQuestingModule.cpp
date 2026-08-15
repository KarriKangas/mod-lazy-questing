#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "TravelMgr.h"

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

    bool IsLowValueTarget(TravelDestination* destination)
    {
        return dynamic_cast<NullTravelDestination*>(destination) ||
               dynamic_cast<GrindTravelDestination*>(destination) ||
               dynamic_cast<ExploreTravelDestination*>(destination);
    }

    bool IsUsableTarget(Player* bot, TravelTarget* target)
    {
        if (!target || !target->getDestination())
            return false;

        if (target->getStatus() == TRAVEL_STATUS_EXPIRED || !target->isActive())
            return false;

        return target->getDestination()->isActive(bot);
    }

    bool ShouldNudge(Player* bot, TravelTarget* current)
    {
        if (!current)
            return false;

        if (current->isForced() || current->isGroupCopy())
            return false;

        TravelDestination* destination = current->getDestination();
        if (!destination || IsLowValueTarget(destination))
            return true;

        if (dynamic_cast<RpgTravelDestination*>(destination))
            return false;

        return !IsUsableTarget(bot, current);
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
        if (!FindLazyQuestCandidate(player, candidate))
        {
            state.lastQuestId = 0;
            state.lastDestination = nullptr;
            state.lastPoint = nullptr;
            return;
        }

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        TravelTarget* current = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

        if (!ShouldNudge(player, current))
        {
            LogCandidate(player, state, candidate);
            return;
        }

        std::string oldTarget = "none";
        if (current && current->getDestination())
            oldTarget = current->getDestination()->getName();

        current->setTarget(candidate.destination, candidate.point);

        state.lastQuestId = candidate.questId;
        state.lastDestination = candidate.destination;
        state.lastPoint = candidate.point;

        Quest const* quest = candidate.destination->GetQuestTemplate();
        LOG_INFO("playerbots", "[LQ] {}: {} -> quest {} [{}] ({}) at {:.0f}y", player->GetName(), oldTarget,
                 candidate.questId, quest ? quest->GetTitle() : "<unknown>", candidate.destination->getTitle(),
                 candidate.distance);
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
