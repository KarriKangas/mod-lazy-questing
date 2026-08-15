#include "LazyQuestSelector.h"

#include "Player.h"
#include "QuestDef.h"
#include "TravelMgr.h"

namespace
{
    constexpr float MAX_QUEST_DISTANCE = 2500.0f;
}

bool FindLazyQuestCandidate(Player* bot, LazyQuestCandidate& candidate)
{
    if (!bot)
        return false;

    WorldPosition botPosition(bot);
    bool found = false;

    for (auto const& quest : bot->getQuestStatusMap())
    {
        uint32 const questId = quest.first;
        QuestStatusData const& questStatus = quest.second;

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

            bool const completed = questStatus.Status == QUEST_STATUS_COMPLETE;
            if (found && (candidate.completed && !completed || candidate.completed == completed && candidate.distance <= distance))
                continue;

            candidate.destination = destination;
            candidate.point = point;
            candidate.questId = questId;
            candidate.completed = completed;
            candidate.distance = distance;
            found = true;
        }
    }

    return found;
}
