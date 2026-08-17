#include "LazyQuestSelector.h"

#include "Player.h"
#include "QuestDef.h"
#include "TravelMgr.h"

#include <vector>

namespace
{
    constexpr float MAX_QUEST_DISTANCE = 2500.0f;
    constexpr float QUEST_CLUSTER_RADIUS = 400.0f;

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
        }
    }

    if (candidates.empty())
        return false;

    // Turning in completed quests remains the highest priority. Keep the old behavior: nearest turn-in wins.
    RankedQuestCandidate const* nearestTurnIn = nullptr;
    for (RankedQuestCandidate const& current : candidates)
    {
        if (current.candidate.type != LazyQuestIntentType::TurnIn)
            continue;

        if (!nearestTurnIn || current.candidate.distance < nearestTurnIn->candidate.distance)
            nearestTurnIn = &current;
    }

    if (nearestTurnIn)
    {
        candidate = nearestTurnIn->candidate;
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
        return false;

    candidate = bestQuestWork->candidate;
    return true;
}
