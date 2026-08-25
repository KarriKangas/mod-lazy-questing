#include "LazyQuestSelector.h"

#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "QuestValues.h"
#include "TravelMgr.h"

#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr float MAX_QUEST_DISTANCE = 2500.0f;
    constexpr float QUEST_CLUSTER_RADIUS = 400.0f;
    constexpr float PICKUP_INDEX_CELL_SIZE = 512.0f;
    constexpr uint32 DESIRED_ACTIVE_QUESTS = 3;

    struct RankedQuestCandidate
    {
        LazyQuestCandidate candidate;
        WorldPosition* clusterPoint = nullptr;
    };

    struct PickupIndexCell
    {
        uint32 mapId = 0;
        int32 x = 0;
        int32 y = 0;

        bool operator==(PickupIndexCell const& other) const
        {
            return mapId == other.mapId && x == other.x && y == other.y;
        }
    };

    struct PickupIndexCellHash
    {
        std::size_t operator()(PickupIndexCell const& cell) const
        {
            std::size_t hash = std::hash<uint32>{}(cell.mapId);
            hash ^= std::hash<int32>{}(cell.x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<int32>{}(cell.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct IndexedPickupPoint
    {
        QuestRelationTravelDestination* destination = nullptr;
        WorldPosition* point = nullptr;
    };

    struct RelationKey
    {
        uint32 questId = 0;
        int32 entry = 0;
        uint32 relation = 0;

        bool operator==(RelationKey const& other) const
        {
            return questId == other.questId && entry == other.entry && relation == other.relation;
        }
    };

    struct RelationKeyHash
    {
        std::size_t operator()(RelationKey const& key) const
        {
            std::size_t hash = std::hash<uint32>{}(key.questId);
            hash ^= std::hash<int32>{}(key.entry) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32>{}(key.relation) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct ObjectiveKey
    {
        uint32 questId = 0;
        int32 entry = 0;
        uint32 objective = 0;
        uint32 itemId = 0;

        bool operator==(ObjectiveKey const& other) const
        {
            return questId == other.questId && entry == other.entry && objective == other.objective &&
                   itemId == other.itemId;
        }
    };

    struct ObjectiveKeyHash
    {
        std::size_t operator()(ObjectiveKey const& key) const
        {
            std::size_t hash = std::hash<uint32>{}(key.questId);
            hash ^= std::hash<int32>{}(key.entry) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32>{}(key.objective) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32>{}(key.itemId) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    using PickupIndex =
        std::unordered_map<PickupIndexCell, std::vector<IndexedPickupPoint>, PickupIndexCellHash>;
    using SpawnPositionIndex = std::unordered_map<int32, std::vector<WorldPosition>>;

    PickupIndex pickupIndex;
    SpawnPositionIndex spawnPositions;
    std::vector<std::unique_ptr<QuestRelationTravelDestination>> relationDestinations;
    std::unordered_map<uint32, std::vector<QuestRelationTravelDestination*>> questRelations;
    std::unordered_map<uint32, std::vector<ObjectiveKey>> questObjectives;
    std::unordered_map<ObjectiveKey, std::unique_ptr<QuestObjectiveTravelDestination>, ObjectiveKeyHash>
        objectiveDestinations;
    LazyQuestIndexStats pickupIndexStats;
    bool pickupIndexReady = false;

    int32 GetCellCoordinate(float coordinate)
    {
        return static_cast<int32>(std::floor(coordinate / PICKUP_INDEX_CELL_SIZE));
    }

    PickupIndexCell GetCell(WorldPosition const& point)
    {
        return { point.GetMapId(), GetCellCoordinate(point.GetPositionX()), GetCellCoordinate(point.GetPositionY()) };
    }

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

    bool HasPointOnBotMap(Player* bot, QuestRelationTravelDestination* destination)
    {
        for (WorldPosition* point : destination->getPoints())
        {
            if (point && point->GetMapId() == bot->GetMapId())
                return true;
        }

        return false;
    }

    WorldPosition* FindNearestSpawnOnMap(int32 entry, WorldPosition const& botPosition, float maxDistance)
    {
        auto positions = spawnPositions.find(entry);
        if (positions == spawnPositions.end())
            return nullptr;

        WorldPosition* nearest = nullptr;
        float nearestDistance = maxDistance;

        for (WorldPosition& point : positions->second)
        {
            if (point.GetMapId() != botPosition.GetMapId())
                continue;

            float const distance = point.distance(botPosition);
            if (distance <= maxDistance && (!nearest || distance < nearestDistance))
            {
                nearest = &point;
                nearestDistance = distance;
            }
        }

        return nearest;
    }

    bool HasAcceptDialogStatus(Player* bot, QuestRelationTravelDestination* destination, uint32 questId,
                               bool allowNormalLevel)
    {
        uint32 const dialogStatus = DialogStatusValue::getDialogStatus(bot, destination->getEntry(), questId);

        if (dialogStatus == DIALOG_STATUS_LOW_LEVEL_AVAILABLE ||
            dialogStatus == DIALOG_STATUS_LOW_LEVEL_AVAILABLE_REP)
            return true;

        return allowNormalLevel &&
               (dialogStatus == DIALOG_STATUS_AVAILABLE || dialogStatus == DIALOG_STATUS_AVAILABLE_REP);
    }

    bool HasTurnInDialogStatus(Player* bot, QuestRelationTravelDestination* destination, uint32 questId)
    {
        uint32 const dialogStatus = DialogStatusValue::getDialogStatus(bot, destination->getEntry(), questId);
        return dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD ||
               dialogStatus == DIALOG_STATUS_REWARD_REP;
    }

    bool IsQuestRelationActive(Player* bot, QuestRelationTravelDestination* destination,
                               LazyQuestIntentType type, uint32 activeQuestCount)
    {
        if (!bot || !destination || !HasPointOnBotMap(bot, destination))
            return false;

        Quest const* quest = destination->GetQuestTemplate();
        if (!quest)
            return false;

        uint32 const questId = quest->GetQuestId();
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        AiObjectContext* context = botAI ? botAI->GetAiObjectContext() : nullptr;
        if (!context)
            return false;

        if (type == LazyQuestIntentType::PickUp)
        {
            if (destination->getRelation() != 0 || bot->GetQuestStatus(questId) != QUEST_STATUS_NONE ||
                bot->IsQuestRewarded(questId) || activeQuestCount >= DESIRED_ACTIVE_QUESTS)
                return false;

            if (static_cast<int32>(quest->GetQuestLevel()) >= static_cast<int32>(bot->GetLevel()) + 5 ||
                !bot->GetMap() || !bot->GetMap()->GetEntry()->IsWorldMap() ||
                !bot->CanTakeQuest(quest, false) || !bot->CanAddQuest(quest, false))
                return false;

            bool const canFightEqual = context->GetValue<bool>("can fight equal")->Get();
            if (!HasAcceptDialogStatus(bot, destination, questId, canFightEqual))
                return false;

            if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) &&
                !context->GetValue<bool>("can fight boss")->Get())
                return false;

            return true;
        }

        if (type != LazyQuestIntentType::TurnIn || destination->getRelation() == 0 ||
            bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || !bot->CanRewardQuest(quest, false) ||
            !HasTurnInDialogStatus(bot, destination, questId))
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

    void AddRankedCandidate(std::vector<RankedQuestCandidate>& candidates, TravelDestination* destination,
                            WorldPosition* point, uint32 questId, LazyQuestIntentType type,
                            WorldPosition const& botPosition, LazyQuestSelectionStats* stats)
    {
        if (!destination || !point || point->GetMapId() != botPosition.GetMapId())
            return;

        float const distance = point->distance(botPosition);

        RankedQuestCandidate ranked;
        ranked.candidate.destination = destination;
        ranked.candidate.point = point;
        ranked.candidate.questId = questId;
        ranked.candidate.type = type;
        ranked.candidate.distance = distance;
        ranked.clusterPoint = point;
        candidates.push_back(ranked);

        if (stats)
            ++stats->candidatesFound;
    }

    void AddRelationDestination(uint32 questId, int32 entry, uint32 relation,
                                std::unordered_set<RelationKey, RelationKeyHash>& seenRelations)
    {
        RelationKey const key{ questId, entry, relation };
        if (!seenRelations.insert(key).second)
            return;

        auto positions = spawnPositions.find(entry);
        if (positions == spawnPositions.end() || positions->second.empty())
            return;

        auto destination = std::make_unique<QuestRelationTravelDestination>(
            questId, entry, relation, sPlayerbotAIConfig.tooCloseDistance, sPlayerbotAIConfig.sightDistance);
        destination->setExpireDelay(5 * 60 * 1000);
        destination->setMaxVisitors(15, 0);

        for (WorldPosition& point : positions->second)
            destination->addPoint(&point);

        QuestRelationTravelDestination* rawDestination = destination.get();
        questRelations[questId].push_back(rawDestination);

        if (relation == 0)
        {
            for (WorldPosition& point : positions->second)
                pickupIndex[GetCell(point)].push_back({ rawDestination, &point });
        }

        relationDestinations.push_back(std::move(destination));
    }

    void AddObjectiveSpec(uint32 questId, int32 entry, uint32 objective, uint32 itemId)
    {
        if (entry == 0 || spawnPositions.find(entry) == spawnPositions.end())
            return;

        ObjectiveKey const key{ questId, entry, objective, itemId };
        std::vector<ObjectiveKey>& objectives = questObjectives[questId];
        for (ObjectiveKey const& existing : objectives)
        {
            if (existing == key)
                return;
        }

        objectives.push_back(key);
    }

    QuestObjectiveTravelDestination* GetOrCreateObjectiveDestination(ObjectiveKey const& key)
    {
        auto existing = objectiveDestinations.find(key);
        if (existing != objectiveDestinations.end())
            return existing->second.get();

        auto positions = spawnPositions.find(key.entry);
        if (positions == spawnPositions.end() || positions->second.empty())
            return nullptr;

        auto destination = std::make_unique<QuestObjectiveTravelDestination>(
            key.questId, key.entry, key.objective, sPlayerbotAIConfig.tooCloseDistance,
            sPlayerbotAIConfig.sightDistance, key.itemId);
        destination->setExpireDelay(60 * 1000);
        destination->setMaxVisitors(100, 1);

        for (WorldPosition& point : positions->second)
            destination->addPoint(&point);

        QuestObjectiveTravelDestination* rawDestination = destination.get();
        objectiveDestinations.emplace(key, std::move(destination));
        return rawDestination;
    }
}

bool InitializeLazyQuestIndex()
{
    if (pickupIndexReady)
        return true;

    LOG_INFO("server.loading", "mod-lazy-questing is building its shared quest catalog.");
    SpawnPositionIndex newSpawnPositions;
    std::size_t spawnPointCount = 0;

    for (auto const& spawn : sObjectMgr->GetAllCreatureData())
    {
        CreatureData const& data = spawn.second;
        newSpawnPositions[static_cast<int32>(data.id)].emplace_back(
            data.mapid, data.posX, data.posY, data.posZ, data.orientation);
        ++spawnPointCount;
    }

    for (auto const& spawn : sObjectMgr->GetAllGOData())
    {
        GameObjectData const& data = spawn.second;
        newSpawnPositions[-static_cast<int32>(data.id)].emplace_back(
            data.mapid, data.posX, data.posY, data.posZ, data.orientation);
        ++spawnPointCount;
    }

    if (newSpawnPositions.empty())
        return false;

    spawnPositions = std::move(newSpawnPositions);
    pickupIndex.reserve(sObjectMgr->GetQuestTemplates().size() / 8);
    relationDestinations.reserve(sObjectMgr->GetQuestTemplates().size());

    std::unordered_set<RelationKey, RelationKeyHash> seenRelations;
    seenRelations.reserve(sObjectMgr->GetQuestTemplates().size());

    for (auto const& relation : *sObjectMgr->GetCreatureQuestRelationMap())
        AddRelationDestination(relation.second, static_cast<int32>(relation.first), 0, seenRelations);
    for (auto const& relation : *sObjectMgr->GetCreatureQuestInvolvedRelationMap())
        AddRelationDestination(relation.second, static_cast<int32>(relation.first), 1, seenRelations);
    for (auto const& relation : *sObjectMgr->GetGOQuestRelationMap())
        AddRelationDestination(relation.second, -static_cast<int32>(relation.first), 0, seenRelations);
    for (auto const& relation : *sObjectMgr->GetGOQuestInvolvedRelationMap())
        AddRelationDestination(relation.second, -static_cast<int32>(relation.first), 1, seenRelations);

    LOG_INFO("server.loading", "mod-lazy-questing cataloged {} spawn points and {} quest relations.",
             spawnPointCount, relationDestinations.size());
    std::unordered_map<uint32, std::vector<int32>> itemSources;
    for (auto const& source : *sObjectMgr->GetCreatureQuestItemMap())
    {
        for (uint32 itemId : source.second)
            itemSources[itemId].push_back(static_cast<int32>(source.first));
    }
    for (auto const& source : *sObjectMgr->GetGameObjectQuestItemMap())
    {
        for (uint32 itemId : source.second)
            itemSources[itemId].push_back(-static_cast<int32>(source.first));
    }
    LOG_INFO("server.loading", "mod-lazy-questing cataloged sources for {} quest items.", itemSources.size());

    for (auto const& questEntry : sObjectMgr->GetQuestTemplates())
    {
        uint32 const questId = questEntry.first;
        Quest const* quest = questEntry.second;
        if (!quest)
            continue;

        for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; ++objective)
        {
            if (quest->RequiredNpcOrGoCount[objective] > 0)
                AddObjectiveSpec(questId, quest->RequiredNpcOrGo[objective], objective, 0);

            if (quest->RequiredItemCount[objective] > 0)
            {
                auto const sources = itemSources.find(quest->RequiredItemId[objective]);
                if (sources != itemSources.end())
                {
                    for (int32 sourceEntry : sources->second)
                        AddObjectiveSpec(questId, sourceEntry, objective, quest->RequiredItemId[objective]);
                }
            }
        }
    }

    std::size_t pickupPointCount = 0;
    for (auto const& cell : pickupIndex)
        pickupPointCount += cell.second.size();

    if (pickupPointCount == 0)
        return false;

    LOG_INFO("server.loading", "mod-lazy-questing cataloged objectives for {} quests.", questObjectives.size());
    pickupIndexStats = { pickupIndex.size(), pickupPointCount };
    pickupIndexReady = true;
    return true;
}

bool IsLazyQuestIndexReady()
{
    return pickupIndexReady;
}

LazyQuestIndexStats GetLazyQuestIndexStats()
{
    return pickupIndexStats;
}

bool IsLazyQuestDestinationActive(Player* bot, TravelDestination* destination, LazyQuestIntentType type)
{
    if (!bot || !destination)
        return false;

    if (auto* relation = dynamic_cast<QuestRelationTravelDestination*>(destination))
        return IsQuestRelationActive(bot, relation, type, CountActiveQuests(bot));

    return type == LazyQuestIntentType::DoQuest &&
           dynamic_cast<QuestObjectiveTravelDestination*>(destination) && destination->isActive(bot);
}

bool FindLazyQuestCandidate(Player* bot, LazyQuestCandidate& candidate,
                            std::unordered_set<uint32> const* excludedQuestIds,
                            LazyQuestSelectionStats* stats)
{
    if (!bot || !pickupIndexReady)
        return false;

    uint32 const activeQuestCount = CountActiveQuests(bot);
    WorldPosition botPosition(bot);
    std::vector<RankedQuestCandidate> candidates;

    for (auto const& quest : bot->getQuestStatusMap())
    {
        uint32 const questId = quest.first;
        QuestStatusData const& questStatus = quest.second;

        if ((excludedQuestIds && excludedQuestIds->count(questId)) || bot->IsQuestRewarded(questId))
            continue;

        if (questStatus.Status != QUEST_STATUS_INCOMPLETE && questStatus.Status != QUEST_STATUS_COMPLETE)
            continue;

        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
        if (!questTemplate)
            continue;

        if (questStatus.Status == QUEST_STATUS_COMPLETE)
        {
            auto const relations = questRelations.find(questId);
            if (relations == questRelations.end())
                continue;

            for (QuestRelationTravelDestination* destination : relations->second)
            {
                if (!IsQuestRelationActive(bot, destination, LazyQuestIntentType::TurnIn, activeQuestCount))
                    continue;

                std::vector<WorldPosition*> points = destination->nextPoint(&botPosition);
                if (points.empty() || !points.front() || points.front()->GetMapId() != bot->GetMapId() ||
                    points.front()->distance(&botPosition) > MAX_QUEST_DISTANCE)
                    continue;

                AddRankedCandidate(candidates, destination, points.front(), questId,
                                   LazyQuestIntentType::TurnIn, botPosition, stats);
            }

            continue;
        }

        auto const objectives = questObjectives.find(questId);
        if (objectives == questObjectives.end())
            continue;

        for (ObjectiveKey const& objective : objectives->second)
        {
            WorldPosition* point = FindNearestSpawnOnMap(objective.entry, botPosition, MAX_QUEST_DISTANCE);
            if (!point)
                continue;

            QuestObjectiveTravelDestination* destination = GetOrCreateObjectiveDestination(objective);
            if (!destination || !destination->isActive(bot))
                continue;

            AddRankedCandidate(candidates, destination, point, questId,
                               LazyQuestIntentType::DoQuest, botPosition, stats);
        }
    }

    if (activeQuestCount < DESIRED_ACTIVE_QUESTS)
    {
        float const pickupSearchDistance = 400.0f + bot->GetLevel() * 10.0f;
        int32 const cellRadius =
            static_cast<int32>(std::ceil(pickupSearchDistance / PICKUP_INDEX_CELL_SIZE));
        PickupIndexCell const center = GetCell(botPosition);
        std::unordered_set<QuestRelationTravelDestination*> localDestinations;

        for (int32 x = center.x - cellRadius; x <= center.x + cellRadius; ++x)
        {
            for (int32 y = center.y - cellRadius; y <= center.y + cellRadius; ++y)
            {
                auto const cell = pickupIndex.find({ center.mapId, x, y });
                if (cell == pickupIndex.end())
                    continue;

                for (IndexedPickupPoint const& indexed : cell->second)
                {
                    if (stats)
                        ++stats->indexedPointsVisited;

                    if (indexed.point->distance(&botPosition) <= pickupSearchDistance)
                        localDestinations.insert(indexed.destination);
                }
            }
        }

        for (QuestRelationTravelDestination* destination : localDestinations)
        {
            if (stats)
                ++stats->pickupDestinationsEvaluated;

            Quest const* quest = destination->GetQuestTemplate();
            if (!quest || (excludedQuestIds && excludedQuestIds->count(quest->GetQuestId())) ||
                !IsQuestRelationActive(bot, destination, LazyQuestIntentType::PickUp, activeQuestCount))
                continue;

            std::vector<WorldPosition*> points = destination->nextPoint(&botPosition);
            if (points.empty() || !points.front() || points.front()->GetMapId() != bot->GetMapId())
                continue;

            WorldPosition* point = points.front();
            if (point->distance(&botPosition) > pickupSearchDistance)
                continue;

            AddRankedCandidate(candidates, destination, point, quest->GetQuestId(),
                               LazyQuestIntentType::PickUp, botPosition, stats);
        }
    }

    if (candidates.empty())
        return false;

    RankedQuestCandidate const* nearestTurnIn = FindNearestCandidate(candidates, LazyQuestIntentType::TurnIn);
    if (nearestTurnIn)
    {
        candidate = nearestTurnIn->candidate;
        return true;
    }

    RankedQuestCandidate const* nearestPickup = FindNearestCandidate(candidates, LazyQuestIntentType::PickUp);
    if (nearestPickup)
    {
        candidate = nearestPickup->candidate;
        return true;
    }

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
