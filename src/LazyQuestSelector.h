#ifndef MOD_LAZY_QUESTING_LAZY_QUEST_SELECTOR_H
#define MOD_LAZY_QUESTING_LAZY_QUEST_SELECTOR_H

#include "Define.h"

#include <cstddef>
#include <unordered_set>
#include <vector>

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

struct LazyQuestSelectionStats
{
    uint32 indexedPointsVisited = 0;
    uint32 pickupDestinationsEvaluated = 0;
    uint32 candidatesFound = 0;
};

struct LazyQuestIndexStats
{
    std::size_t cells = 0;
    std::size_t points = 0;
};

bool InitializeLazyQuestIndex();
bool IsLazyQuestIndexReady();
LazyQuestIndexStats GetLazyQuestIndexStats();

bool FindLazyQuestCandidate(Player* bot, LazyQuestCandidate& candidate,
                            std::unordered_set<uint32> const* excludedQuestIds = nullptr,
                            LazyQuestSelectionStats* stats = nullptr,
                            bool allowQuestWork = true);
bool FindLazyQuestLeg(Player* bot, uint32 questId, LazyQuestIntentType type,
                      TravelDestination* preferredDestination,
                      std::vector<WorldPosition*> const& excludedPoints,
                      LazyQuestCandidate& candidate);
bool IsLazyQuestDestinationActive(Player* bot, TravelDestination* destination, LazyQuestIntentType type);

#endif
