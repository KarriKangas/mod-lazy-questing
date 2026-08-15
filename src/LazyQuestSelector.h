#ifndef MOD_LAZY_QUESTING_LAZY_QUEST_SELECTOR_H
#define MOD_LAZY_QUESTING_LAZY_QUEST_SELECTOR_H

#include "Define.h"

class Player;
class TravelDestination;
class WorldPosition;

struct LazyQuestCandidate
{
    TravelDestination* destination = nullptr;
    WorldPosition* point = nullptr;
    uint32 questId = 0;
    bool completed = false;
    float distance = 0.0f;
};

bool FindLazyQuestCandidate(Player* bot, LazyQuestCandidate& candidate);

#endif
