#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "TravelMgr.h"

#include <unordered_map>
#include <unordered_set>

#include "LazyQuestSelector.h"

namespace
{
    constexpr uint32 CHECK_INTERVAL_MS = 3 * IN_MILLISECONDS;
    constexpr uint32 NO_PROGRESS_TIMEOUT_MS = 5 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 FAILED_QUEST_COOLDOWN_MS = 20 * MINUTE * IN_MILLISECONDS;
    constexpr float TRAVEL_PROGRESS_YARDS = 50.0f;

    char const* IntentTypeName(LazyQuestIntentType type)
    {
        switch (type)
        {
            case LazyQuestIntentType::TurnIn:
                return "turn-in";
            case LazyQuestIntentType::DoQuest:
            default:
                return "do-quest";
        }
    }

    struct LazyQuestIntent
    {
        uint32 questId = 0;
        LazyQuestIntentType type = LazyQuestIntentType::DoQuest;
        uint32 startedAtMs = 0;
        uint32 lastProgressAtMs = 0;
        uint64 progressFingerprint = 0;
        float lastDistance = 0.0f;
        TravelDestination* destination = nullptr;
        WorldPosition* point = nullptr;

        bool IsActive() const { return questId != 0 && destination && point; }

        void Clear()
        {
            questId = 0;
            type = LazyQuestIntentType::DoQuest;
            startedAtMs = 0;
            lastProgressAtMs = 0;
            progressFingerprint = 0;
            lastDistance = 0.0f;
            destination = nullptr;
            point = nullptr;
        }
    };

    struct LazyBotState
    {
        uint32 lastCheckMs = 0;
        LazyQuestIntent intent;
        std::unordered_map<uint32, uint32> questRetryAfterMs;
        bool strategyOwnershipActive = false;
        bool addedTravelStrategy = false;
        bool removedNewRpgStrategy = false;
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

    uint64 GetQuestProgressFingerprint(Player* bot, uint32 questId)
    {
        auto const itr = bot->getQuestStatusMap().find(questId);
        if (itr == bot->getQuestStatusMap().end())
            return 0;

        QuestStatusData const& status = itr->second;
        uint64 fingerprint = status.Status;

        for (uint16 count : status.ItemCount)
            fingerprint = fingerprint * 131 + count;

        for (uint16 count : status.CreatureOrGOCount)
            fingerprint = fingerprint * 131 + count;

        fingerprint = fingerprint * 131 + status.PlayerCount;
        fingerprint = fingerprint * 131 + (status.Explored ? 1 : 0);
        return fingerprint;
    }

    bool IsIntentValid(Player* bot, LazyQuestIntent const& intent)
    {
        if (!intent.IsActive() || bot->IsQuestRewarded(intent.questId) || !intent.destination->isActive(bot))
            return false;

        uint8 status = bot->GetQuestStatus(intent.questId);
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
            return false;

        LazyQuestIntentType currentType = status == QUEST_STATUS_COMPLETE
            ? LazyQuestIntentType::TurnIn
            : LazyQuestIntentType::DoQuest;
        return currentType == intent.type;
    }

    void AcquireStrategyOwnership(Player* bot, PlayerbotAI* botAI, LazyBotState& state)
    {
        if (state.strategyOwnershipActive)
            return;

        bool hadTravel = botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT);
        bool hadNewRpg = botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);

        state.addedTravelStrategy = !hadTravel;
        state.removedNewRpgStrategy = hadNewRpg;

        if (state.addedTravelStrategy)
            botAI->ChangeStrategy("+travel", BOT_STATE_NON_COMBAT);

        if (state.removedNewRpgStrategy)
            botAI->ChangeStrategy("-new rpg", BOT_STATE_NON_COMBAT);

        state.strategyOwnershipActive = true;

        LOG_INFO("playerbots",
                 "[LQ] {} took movement control: travel {} -> {}, new-rpg {} -> {}, rpg-status {}",
                 bot->GetName(), hadTravel ? "on" : "off",
                 botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT) ? "on" : "off",
                 hadNewRpg ? "on" : "off",
                 botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) ? "on" : "off",
                 static_cast<uint32>(botAI->rpgInfo.GetStatus()));
    }

    void ReleaseStrategyOwnership(Player* bot, PlayerbotAI* botAI, LazyBotState& state)
    {
        if (!state.strategyOwnershipActive)
            return;

        bool removedTravel = state.addedTravelStrategy && botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT);
        bool restoredNewRpg = state.removedNewRpgStrategy && !botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);

        if (removedTravel)
            botAI->ChangeStrategy("-travel", BOT_STATE_NON_COMBAT);

        if (restoredNewRpg)
            botAI->ChangeStrategy("+new rpg", BOT_STATE_NON_COMBAT);

        LOG_INFO("playerbots",
                 "[LQ] {} released movement control: travel={}, new-rpg={}, restored travel={}, restored new-rpg={}",
                 bot->GetName(), botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT) ? "on" : "off",
                 botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) ? "on" : "off",
                 removedTravel ? "yes" : "no", restoredNewRpg ? "yes" : "no");

        state.strategyOwnershipActive = false;
        state.addedTravelStrategy = false;
        state.removedNewRpgStrategy = false;
    }

    void ReleaseIntent(Player* bot, PlayerbotAI* botAI, LazyBotState& state, char const* reason)
    {
        if (!state.intent.IsActive())
            return;

        LOG_INFO("playerbots", "[LQ] {} released {} quest {} intent ({})", bot->GetName(),
                 IntentTypeName(state.intent.type), state.intent.questId, reason);
        state.intent.Clear();
        ReleaseStrategyOwnership(bot, botAI, state);
    }

    void AcquireIntent(Player* bot, PlayerbotAI* botAI, LazyBotState& state, LazyQuestCandidate const& candidate, uint32 now)
    {
        state.intent.questId = candidate.questId;
        state.intent.type = candidate.type;
        state.intent.startedAtMs = now;
        state.intent.lastProgressAtMs = now;
        state.intent.progressFingerprint = GetQuestProgressFingerprint(bot, candidate.questId);
        state.intent.lastDistance = candidate.distance;
        state.intent.destination = candidate.destination;
        state.intent.point = candidate.point;

        AcquireStrategyOwnership(bot, botAI, state);

        Quest const* quest = candidate.destination->GetQuestTemplate();
        LOG_INFO("playerbots", "[LQ] {} acquired {} quest {} [{}] intent at {:.0f}y", bot->GetName(),
                 IntentTypeName(candidate.type), candidate.questId, quest ? quest->GetTitle() : "<unknown>",
                 candidate.distance);
    }

    void MaintainIntent(Player* bot, LazyBotState& state, TravelTarget* current)
    {
        if (!current || current->isForced() || current->isGroupCopy())
            return;

        if (current->getDestination() == state.intent.destination && IsUsableTarget(bot, current))
            return;

        current->setTarget(state.intent.destination, state.intent.point);
        LOG_INFO("playerbots", "[LQ] {} restored {} quest {} travel target", bot->GetName(),
                 IntentTypeName(state.intent.type), state.intent.questId);
    }

    void UpdateIntentProgress(Player* bot, LazyQuestIntent& intent, uint32 now)
    {
        uint64 fingerprint = GetQuestProgressFingerprint(bot, intent.questId);
        WorldPosition botPosition(bot);
        float distance = intent.point->distance(&botPosition);

        if (fingerprint != intent.progressFingerprint || distance + TRAVEL_PROGRESS_YARDS < intent.lastDistance)
        {
            intent.progressFingerprint = fingerprint;
            intent.lastDistance = distance;
            intent.lastProgressAtMs = now;
        }
    }

    std::unordered_set<uint32> GetCoolingDownQuests(LazyBotState& state, uint32 now)
    {
        std::unordered_set<uint32> coolingDown;

        for (auto itr = state.questRetryAfterMs.begin(); itr != state.questRetryAfterMs.end();)
        {
            if (now >= itr->second)
            {
                itr = state.questRetryAfterMs.erase(itr);
                continue;
            }

            coolingDown.insert(itr->first);
            ++itr;
        }

        return coolingDown;
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
                UpdateIntentProgress(player, state.intent, now);

                if (getMSTimeDiff(state.intent.lastProgressAtMs, now) < NO_PROGRESS_TIMEOUT_MS)
                {
                    MaintainIntent(player, state, current);
                    return;
                }

                uint32 stalledQuestId = state.intent.questId;
                TravelDestination* stalledDestination = state.intent.destination;
                state.questRetryAfterMs[stalledQuestId] = now + FAILED_QUEST_COOLDOWN_MS;

                if (current && current->getDestination() == stalledDestination)
                    current->setStatus(TRAVEL_STATUS_EXPIRED);

                LOG_INFO("playerbots", "[LQ] {} cooling down stalled quest {} for 20 minutes", player->GetName(),
                         stalledQuestId);
                ReleaseIntent(player, botAI, state, "no progress for 5 minutes");
            }
            else
            {
                ReleaseIntent(player, botAI, state, "quest state changed");
            }
        }

        std::unordered_set<uint32> coolingDown = GetCoolingDownQuests(state, now);
        LazyQuestCandidate candidate;
        if (!FindLazyQuestCandidate(player, candidate, &coolingDown) || !ShouldNudge(player, current))
            return;

        AcquireIntent(player, botAI, state, candidate, now);
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
