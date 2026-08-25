#include "LazyQuestSelector.h"

#include "Player.h"
#include "QuestDef.h"
#include "TravelMgr.h"

#include <vector>

namespace
{
    constexpr float MAX_QUEST_DISTANCE = 2500.0f;
    constexpr float LOCAL_QUEST_PICKUP_DISTANCE = 250.0f;
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
}

bool FindLazyQuestCandidate(Player* bot, LazyQuestCandidate& candidate,
                            std::unordered_set<uint32> const* excludedQuestIds)
{
    if (!bot)
        return false;

    WorldPosition botPosition(bot);
    std::vector<RankedQuestCandidate> candidates;
    std::unordered_set<uint32> activeQuestIds;

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
            TravelMgr::instance().getQuestTravelDestinations(bot, questId, true, false, MAX_QUEST_DISTANCE);

        for (TravelDestination* destination : destinations)
        {
            if (!destination || !destination->isActive(bot))
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
            ranked.candidate.type = questStatus.Status == QUEST_STATUS_COMPLETE
                ? LazyQuestIntentType::TurnIn
                : LazyQuestIntentType::DoQuest;
            ranked.candidate.distance = distance;
            ranked.clusterPoint = destination->nearestPoint(&botPosition);
            candidates.push_back(ranked);

            if (ranked.candidate.type == LazyQuestIntentType::DoQuest)
                activeQuestIds.insert(questId);
        }
    }

    // Stock Playerbots discovers available quest givers but fails to build the point list needed
    // to select one. Reuse those already-filtered destinations so bots can deliberately refill
    // sparse quest logs and pick up follow-up quests while they are still near the quest hub.
    float const pickupSearchDistance = 400.0f + bot->GetLevel() * 10.0f;
    std::vector<TravelDestination*> pickupDestinations =
        TravelMgr::instance().getQuestTravelDestinations(bot, -1, true, false, pickupSearchDistance);

    for (TravelDestination* destination : pickupDestinations)
    {
        if (!destination || !destination->isActive(bot))
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

    if (candidates.empty())
        return false;

    // Turning in completed quests remains the highest priority. Keep the old behavior: nearest turn-in wins.
    RankedQuestCandidate const* nearestTurnIn = FindNearestCandidate(candidates, LazyQuestIntentType::TurnIn);

    if (nearestTurnIn)
    {
        candidate = nearestTurnIn->candidate;
        return true;
    }

    // Pick up follow-ups and other quests in the current hub. Travel farther for a new quest only
    // when the bot has too little actionable quest work to keep questing productively.
    RankedQuestCandidate const* nearestPickup = FindNearestCandidate(candidates, LazyQuestIntentType::PickUp);
    if (nearestPickup && (nearestPickup->candidate.distance <= LOCAL_QUEST_PICKUP_DISTANCE ||
                          activeQuestIds.size() < DESIRED_ACTIVE_QUESTS))
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
