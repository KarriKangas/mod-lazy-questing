#ifndef MOD_LAZY_QUESTING_LAZY_QUEST_SELECTOR_H
#define MOD_LAZY_QUESTING_LAZY_QUEST_SELECTOR_H

#include "Define.h"

#include <unordered_set>

class Player;
class TravelDestination;
class WorldPosition;

enum class LazyQuestIntentType : uint8
{
    PickUp,
    DoQuest,
    TurnIn,
};

struct LazyQuestCandidate
{
    TravelDestination* destination = nullptr;
    WorldPosition* point = nullptr;
    uint32 questId = 0;
    LazyQuestIntentType type = LazyQuestIntentType::DoQuest;
    float distance = 0.0f;
};

bool FindLazyQuestCandidate(Player* bot, LazyQuestCandidate& candidate,
                            std::unordered_set<uint32> const* excludedQuestIds = nullptr);

#endif
