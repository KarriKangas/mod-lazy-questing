#include "LazyQuestSelector.h"

#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "TravelMgr.h"

#include <vector>

namespace
{
    constexpr float MAX_QUEST_DISTANCE = 2500.0f;
    constexpr float QUEST_CLUSTER_RADIUS = 400.0f;
    constexpr uint32 DESIRED_ACTIVE_QUESTS = 3;

    struct RankedQuestCandidate
    {
        LazyQuestCandidate candidate;
        WorldPosition* clusterPoint = nullptr;
    };

    uint32 CountNearbyQuestWork(RankedQuestCandidate const& current,
                                std::vector<RankedQuestCandidate> const& candidates)
    {
        uint32 count = 1;

        for (RankedQuestCandidate const& other : candidates)
        {
            if (&other == &current || other.candidate.type != LazyQuestIntentType::DoQuest)
                continue;

            if (!current.clusterPoint || !other.clusterPoint ||
                current.clusterPoint->GetMapId() != other.clusterPoint->GetMapId())
                continue;

            if (current.clusterPoint->distance(other.clusterPoint) <= QUEST_CLUSTER_RADIUS)
                ++count;
        }

        return count;
    }

    RankedQuestCandidate const* FindNearestCandidate(std::vector<RankedQuestCandidate> const& candidates,
                                                      LazyQuestIntentType type)
    {
        RankedQuestCandidate const* nearest = nullptr;

        for (RankedQuestCandidate const& current : candidates)
        {
            if (current.candidate.type != type)
                continue;

            if (!nearest || current.candidate.distance < nearest->candidate.distance)
                nearest = &current;
        }

        return nearest;
    }

    uint32 CountActiveQuests(Player* bot)
    {
        uint32 count = 0;

        for (auto const& quest : bot->getQuestStatusMap())
        {
            QuestStatus const status = quest.second.Status;
            if (!bot->IsQuestRewarded(quest.first) &&
                (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE))
                ++count;
        }

        return count;
    }

    bool IsQuestRelationActive(Player* bot, QuestRelationTravelDestination* destination,
                               LazyQuestIntentType type)
    {
        if (!bot || !destination)
            return false;

        Quest const* quest = destination->GetQuestTemplate();
        if (!quest)
            return false;

        uint32 const questId = quest->GetQuestId();
        std::vector<WorldPosition*> const& points = destination->getPoints();
        if (points.empty() || points.front()->GetMapId() != bot->GetMapId())
            return false;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        AiObjectContext* context = botAI ? botAI->GetAiObjectContext() : nullptr;
        if (!context)
            return false;

        if (type == LazyQuestIntentType::PickUp)
        {
            if (destination->getRelation() != 0 || bot->GetQuestStatus(questId) != QUEST_STATUS_NONE ||
                bot->IsQuestRewarded(questId) || CountActiveQuests(bot) >= DESIRED_ACTIVE_QUESTS)
                return false;

            if (static_cast<int32>(quest->GetQuestLevel()) >= static_cast<int32>(bot->GetLevel()) + 5 ||
                !bot->GetMap()->GetEntry()->IsWorldMap() || !bot->CanTakeQuest(quest, false) ||
                !bot->CanAddQuest(quest, false))
                return false;

            std::string const entry = std::to_string(destination->getEntry());
            if (context->GetValue<bool>("can fight equal")->Get())
            {
                bool const canAccept = context->GetValue<bool>(
                    "group or", "following party,near leader,can accept quest npc::" + entry)->Get();
                bool const canAcceptLowLevel = context->GetValue<bool>(
                    "group or", "following party,near leader,can accept quest low level npc::" + entry +
                                    "need quest objective::" + std::to_string(questId))->Get();
                if (!canAccept && !canAcceptLowLevel)
                    return false;
            }
            else if (!context->GetValue<bool>(
                         "group or", "following party,near leader,can accept quest low level npc::" + entry)->Get())
            {
                return false;
            }

            if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) &&
                !context->GetValue<bool>("can fight boss")->Get())
                return false;

            return true;
        }

        if (type != LazyQuestIntentType::TurnIn || destination->getRelation() == 0 ||
            bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || !bot->CanRewardQuest(quest, false))
            return false;

        if (!context->GetValue<bool>(
                 "group or", "following party,near leader,can turn in quest npc::" +
                                 std::to_string(destination->getEntry()))->Get())
            return false;

        if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) &&
            !context->GetValue<bool>("can fight boss")->Get())
        {
            WorldPosition botPosition(bot);
            WorldPosition* nearest = destination->nearestPoint(&botPosition);
            if (nearest && !nearest->isOverworld())
                return false;
        }

        return true;
    }
}

bool IsLazyQuestDestinationActive(Player* bot, TravelDestination* destination, LazyQuestIntentType type)
{
    if (!bot || !destination)
        return false;

    if (auto* relation = dynamic_cast<QuestRelationTravelDestination*>(destination))
        return IsQuestRelationActive(bot, relation, type);

    return type == LazyQuestIntentType::DoQuest &&
           dynamic_cast<QuestObjectiveTravelDestination*>(destination) && destination->isActive(bot);
}

bool FindLazyQuestCandidate(Player* bot, LazyQuestCandidate& candidate,
                            std::unordered_set<uint32> const* excludedQuestIds)
{
    if (!bot)
        return false;

    WorldPosition botPosition(bot);
    std::vector<RankedQuestCandidate> candidates;

    for (auto const& quest : bot->getQuestStatusMap())
    {
        uint32 const questId = quest.first;
        QuestStatusData const& questStatus = quest.second;

        if (excludedQuestIds && excludedQuestIds->count(questId))
            continue;

        if (bot->IsQuestRewarded(questId))
            continue;

        if (questStatus.Status != QUEST_STATUS_INCOMPLETE && questStatus.Status != QUEST_STATUS_COMPLETE)
            continue;

        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
        if (!questTemplate)
            continue;

        std::vector<TravelDestination*> destinations =
            TravelMgr::instance().getQuestTravelDestinations(bot, questId, true, true, MAX_QUEST_DISTANCE);

        for (TravelDestination* destination : destinations)
        {
            LazyQuestIntentType const type = questStatus.Status == QUEST_STATUS_COMPLETE
                ? LazyQuestIntentType::TurnIn
                : LazyQuestIntentType::DoQuest;
            if (!IsLazyQuestDestinationActive(bot, destination, type))
                continue;

            std::vector<WorldPosition*> points = destination->nextPoint(&botPosition);
            if (points.empty())
                continue;

            WorldPosition* point = points.front();
            float const distance = point->distance(&botPosition);
            if (distance > MAX_QUEST_DISTANCE)
                continue;

            RankedQuestCandidate ranked;
            ranked.candidate.destination = destination;
            ranked.candidate.point = point;
            ranked.candidate.questId = questId;
            ranked.candidate.type = type;
            ranked.candidate.distance = distance;
            ranked.clusterPoint = destination->nearestPoint(&botPosition);
            candidates.push_back(ranked);
        }
    }

    // Playerbots' quest relation destination currently checks for the obsolete "rpg quest"
    // strategy, so ask for relation destinations without that filter and validate them above.
    // Skip the global giver scan altogether once the intentionally small quest log is full.
    if (CountActiveQuests(bot) < DESIRED_ACTIVE_QUESTS)
    {
        float const pickupSearchDistance = 400.0f + bot->GetLevel() * 10.0f;
        std::vector<TravelDestination*> pickupDestinations =
            TravelMgr::instance().getQuestTravelDestinations(bot, -1, true, true, pickupSearchDistance);

        for (TravelDestination* destination : pickupDestinations)
        {
            if (!IsLazyQuestDestinationActive(bot, destination, LazyQuestIntentType::PickUp))
                continue;

            Quest const* quest = destination->GetQuestTemplate();
            if (!quest || (excludedQuestIds && excludedQuestIds->count(quest->GetQuestId())))
                continue;

            std::vector<WorldPosition*> points = destination->nextPoint(&botPosition);
            if (points.empty())
                continue;

            WorldPosition* point = points.front();
            float const distance = point->distance(&botPosition);
            if (distance > pickupSearchDistance)
                continue;

            RankedQuestCandidate ranked;
            ranked.candidate.destination = destination;
            ranked.candidate.point = point;
            ranked.candidate.questId = quest->GetQuestId();
            ranked.candidate.type = LazyQuestIntentType::PickUp;
            ranked.candidate.distance = distance;
            ranked.clusterPoint = destination->nearestPoint(&botPosition);
            candidates.push_back(ranked);
        }
    }

    if (candidates.empty())
        return false;

    // Turning in completed quests remains the highest priority. Keep the old behavior: nearest turn-in wins.
    RankedQuestCandidate const* nearestTurnIn = FindNearestCandidate(candidates, LazyQuestIntentType::TurnIn);

    if (nearestTurnIn)
    {
        candidate = nearestTurnIn->candidate;
        return true;
    }

    // Pick up only enough quests to maintain a deliberately small, focused quest log.
    RankedQuestCandidate const* nearestPickup = FindNearestCandidate(candidates, LazyQuestIntentType::PickUp);
    if (nearestPickup)
    {
        candidate = nearestPickup->candidate;
        return true;
    }

    // For unfinished quests, prefer destinations surrounded by other useful quest work.
    // At the tiny candidate counts in a quest log, the simple O(n^2) scan is plenty.
    RankedQuestCandidate const* bestQuestWork = nullptr;
    uint32 bestClusterSize = 0;

    for (RankedQuestCandidate const& current : candidates)
    {
        if (current.candidate.type != LazyQuestIntentType::DoQuest)
            continue;

        uint32 const clusterSize = CountNearbyQuestWork(current, candidates);
        if (!bestQuestWork || clusterSize > bestClusterSize ||
            (clusterSize == bestClusterSize && current.candidate.distance < bestQuestWork->candidate.distance))
        {
            bestQuestWork = &current;
            bestClusterSize = clusterSize;
        }
    }

    if (!bestQuestWork)
    {
        if (!nearestPickup)
            return false;

        candidate = nearestPickup->candidate;
        return true;
    }

    candidate = bestQuestWork->candidate;
    return true;
}
