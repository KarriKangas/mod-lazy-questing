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

    struct LazyQuestIntent
    {
        uint32 questId = 0;
        bool completed = false;
        uint32 startedAtMs = 0;
        TravelDestination* destination = nullptr;
        WorldPosition* point = nullptr;

        bool IsActive() const { return questId != 0 && destination && point; }

        void Clear()
        {
            questId = 0;
            completed = false;
            startedAtMs = 0;
            destination = nullptr;
            point = nullptr;
        }
    };

    struct LazyBotState
    {
        uint32 lastCheckMs = 0;
        LazyQuestIntent intent;
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

    bool IsIntentValid(Player* bot, LazyQuestIntent const& intent)
    {
        if (!intent.IsActive() || bot->IsQuestRewarded(intent.questId) || !intent.destination->isActive(bot))
            return false;

        uint8 status = bot->GetQuestStatus(intent.questId);
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
            return false;

        bool completed = status == QUEST_STATUS_COMPLETE;
        return completed == intent.completed;
    }

    void ReleaseIntent(Player* bot, LazyBotState& state, char const* reason)
    {
        if (!state.intent.IsActive())
            return;

        LOG_INFO("playerbots", "[LQ] {} released quest {} intent ({})", bot->GetName(), state.intent.questId, reason);
        state.intent.Clear();
    }

    void AcquireIntent(Player* bot, LazyBotState& state, LazyQuestCandidate const& candidate, uint32 now)
    {
        state.intent.questId = candidate.questId;
        state.intent.completed = candidate.completed;
        state.intent.startedAtMs = now;
        state.intent.destination = candidate.destination;
        state.intent.point = candidate.point;

        Quest const* quest = candidate.destination->GetQuestTemplate();
        LOG_INFO("playerbots", "[LQ] {} acquired quest {} [{}] intent ({}) at {:.0f}y", bot->GetName(),
                 candidate.questId, quest ? quest->GetTitle() : "<unknown>",
                 candidate.completed ? "turn-in" : "objective", candidate.distance);
    }

    void MaintainIntent(Player* bot, LazyBotState& state, TravelTarget* current)
    {
        if (!current || current->isForced() || current->isGroupCopy())
            return;

        if (current->getDestination() == state.intent.destination && IsUsableTarget(bot, current))
            return;

        current->setTarget(state.intent.destination, state.intent.point);
        LOG_INFO("playerbots", "[LQ] {} restored quest {} travel target", bot->GetName(), state.intent.questId);
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

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (!botAI)
            return;

        LazyBotState& state = botStates[player->GetGUID().GetRawValue()];
        uint32 now = getMSTime();
        if (!ShouldCheck(state, now))
            return;

        TravelTarget* current = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

        if (state.intent.IsActive())
        {
            if (IsIntentValid(player, state.intent))
            {
                MaintainIntent(player, state, current);
                return;
            }

            ReleaseIntent(player, state, "quest state changed");
        }

        LazyQuestCandidate candidate;
        if (!FindLazyQuestCandidate(player, candidate) || !ShouldNudge(player, current))
            return;

        AcquireIntent(player, state, candidate, now);
        current->setTarget(candidate.destination, candidate.point);
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
