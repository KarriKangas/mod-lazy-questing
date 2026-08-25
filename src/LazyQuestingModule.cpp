#include "Config.h"
#include "Event.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "StrictAltbotMgr.h"
#include "TravelMgr.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LazyQuestSelector.h"

namespace
{
    using SchedulerClock = std::chrono::steady_clock;
    using SchedulerTime = SchedulerClock::time_point;

    constexpr uint32 NO_PROGRESS_TIMEOUT_MS = 5 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 FAILED_QUEST_COOLDOWN_MS = 20 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 QUEST_INTERACTION_RETRY_MIN_MS = 5 * IN_MILLISECONDS;
    constexpr uint32 QUEST_INTERACTION_RETRY_MAX_MS = MINUTE * IN_MILLISECONDS;
    constexpr uint32 TRANSIENT_RETRY_MS = 5 * IN_MILLISECONDS;
    constexpr uint32 POST_INTERACTION_RETRY_MS = IN_MILLISECONDS;
    constexpr std::size_t MAX_PENDING_EVENTS_PER_TICK = 512;
    constexpr uint8 MAX_REGISTRATION_RETRIES = 20;
    constexpr std::size_t MAX_QUEST_COOLDOWNS_PER_BOT = 32;
    constexpr float TRAVEL_PROGRESS_YARDS = 50.0f;

    struct LazyQuestingConfig
    {
        bool enabled = true;
        uint32 activeCheckIntervalMs = 5 * IN_MILLISECONDS;
        uint32 discoveryIntervalMs = 30 * IN_MILLISECONDS;
        uint32 maxDiscoveryBackoffMs = 2 * MINUTE * IN_MILLISECONDS;
        uint32 worldBudgetMs = 2;
        uint32 maxActiveBotsPerTick = 64;
        uint32 maxDiscoveryBotsPerTick = 4;
        uint32 metricsIntervalMs = MINUTE * IN_MILLISECONDS;
    };

    uint32 ClampConfig(uint32 value, uint32 minimum, uint32 maximum)
    {
        return std::max(minimum, std::min(value, maximum));
    }

    LazyQuestingConfig ReadLazyQuestingConfig()
    {
        LazyQuestingConfig config;
        config.enabled = sConfigMgr->GetOption<bool>("LazyQuesting.Enable", true);
        config.activeCheckIntervalMs = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.ActiveCheckIntervalMs", 5000), 1000, 60000);
        config.discoveryIntervalMs = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.DiscoveryIntervalMs", 30000), 5000, 600000);
        config.maxDiscoveryBackoffMs = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.MaxDiscoveryBackoffMs", 120000),
            config.discoveryIntervalMs, 30 * MINUTE * IN_MILLISECONDS);
        config.worldBudgetMs = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.WorldBudgetMs", 2), 1, 20);
        config.maxActiveBotsPerTick = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.MaxActiveBotsPerTick", 64), 1, 1000);
        config.maxDiscoveryBotsPerTick = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.MaxDiscoveryBotsPerTick", 4), 1, 100);
        config.metricsIntervalMs = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.MetricsIntervalMs", 60000), 10000, 3600000);
        return config;
    }

    bool IsLazyQuestingBot(Player* player)
    {
        return player && sStrictAltbotMgr->IsStrictAltbot(player->GetGUID().GetCounter());
    }

    char const* IntentTypeName(LazyQuestIntentType type)
    {
        switch (type)
        {
            case LazyQuestIntentType::PickUp:
                return "pick-up";
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
        uint32 lastInteractionAtMs = 0;
        uint8 interactionFailures = 0;
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
            lastInteractionAtMs = 0;
            interactionFailures = 0;
            progressFingerprint = 0;
            lastDistance = 0.0f;
            destination = nullptr;
            point = nullptr;
        }
    };

    enum class ScheduleLane : uint8
    {
        Active,
        Discovery,
    };

    struct LazyBotState
    {
        LazyQuestIntent intent;
        std::unordered_map<uint32, SchedulerTime> questRetryAfter;
        uint64 scheduleGeneration = 0;
        ScheduleLane scheduledLane = ScheduleLane::Discovery;
        uint8 consecutiveDiscoveryMisses = 0;
        bool strategyOwnershipActive = false;
        bool addedTravelStrategy = false;
        bool removedNewRpgStrategy = false;
    };

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

        TravelDestination* destination = target->getDestination();
        if (auto* relation = dynamic_cast<QuestRelationTravelDestination*>(destination))
        {
            Quest const* quest = relation->GetQuestTemplate();
            if (!quest)
                return false;

            QuestStatus const status = bot->GetQuestStatus(quest->GetQuestId());
            LazyQuestIntentType const type = status == QUEST_STATUS_NONE
                ? LazyQuestIntentType::PickUp
                : LazyQuestIntentType::TurnIn;
            return IsLazyQuestDestinationActive(bot, destination, type);
        }

        return destination->isActive(bot);
    }

    bool HasEssentialRpgNeed(PlayerbotAI* botAI)
    {
        AiObjectContext* context = botAI->GetAiObjectContext();
        bool const needsVendor =
            context->GetValue<bool>("group or", "should sell,can sell,following party,near leader")->Get();
        bool const needsRepair =
            context->GetValue<bool>("group or", "should repair,can repair,following party,near leader")->Get();
        return needsVendor || needsRepair;
    }

    bool ShouldNudge(Player* bot, PlayerbotAI* botAI, TravelTarget* current)
    {
        if (!current || current->isGroupCopy())
            return false;

        TravelDestination* destination = current->getDestination();

        if (dynamic_cast<RpgTravelDestination*>(destination))
        {
            if (HasEssentialRpgNeed(botAI) || (current->isForced() && botAI->HasActivePlayerMaster()))
                return false;

            return true;
        }

        if (current->isForced())
            return false;

        if (!destination || IsLowValueTarget(destination))
            return true;

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
        if (!intent.IsActive() || bot->IsQuestRewarded(intent.questId))
            return false;

        uint8 const status = bot->GetQuestStatus(intent.questId);
        if (status != QUEST_STATUS_NONE && status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
            return false;

        LazyQuestIntentType currentType;
        if (status == QUEST_STATUS_NONE)
            currentType = LazyQuestIntentType::PickUp;
        else if (status == QUEST_STATUS_COMPLETE)
            currentType = LazyQuestIntentType::TurnIn;
        else
            currentType = LazyQuestIntentType::DoQuest;

        return currentType == intent.type &&
               IsLazyQuestDestinationActive(bot, intent.destination, intent.type);
    }

    void AcquireStrategyOwnership(Player* bot, PlayerbotAI* botAI, LazyBotState& state)
    {
        if (state.strategyOwnershipActive)
            return;

        bool const hadTravel = botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT);
        bool const hadNewRpg = botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);

        state.addedTravelStrategy = !hadTravel;
        state.removedNewRpgStrategy = hadNewRpg;

        if (state.addedTravelStrategy)
            botAI->ChangeStrategy("+travel", BOT_STATE_NON_COMBAT);

        if (state.removedNewRpgStrategy)
            botAI->ChangeStrategy("-new rpg", BOT_STATE_NON_COMBAT);

        state.strategyOwnershipActive = true;

        LOG_DEBUG("playerbots",
                  "[LQ] {} took movement control: travel {} -> {}, new-rpg {} -> {}",
                  bot->GetName(), hadTravel ? "on" : "off",
                  botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT) ? "on" : "off",
                  hadNewRpg ? "on" : "off",
                  botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) ? "on" : "off");
    }

    void ReleaseStrategyOwnership(Player* bot, PlayerbotAI* botAI, LazyBotState& state)
    {
        if (!state.strategyOwnershipActive)
            return;

        if (state.addedTravelStrategy && botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT))
            botAI->ChangeStrategy("-travel", BOT_STATE_NON_COMBAT);

        if (state.removedNewRpgStrategy && !botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
            botAI->ChangeStrategy("+new rpg", BOT_STATE_NON_COMBAT);

        LOG_DEBUG("playerbots", "[LQ] {} released movement control", bot->GetName());

        state.strategyOwnershipActive = false;
        state.addedTravelStrategy = false;
        state.removedNewRpgStrategy = false;
    }

    void ReleaseIntent(Player* bot, PlayerbotAI* botAI, LazyBotState& state, char const* reason)
    {
        if (!state.intent.IsActive())
            return;

        TravelTarget* current = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
        if (current && current->getDestination() == state.intent.destination)
        {
            current->setForced(false);
            current->setStatus(TRAVEL_STATUS_EXPIRED);
        }

        LOG_DEBUG("playerbots", "[LQ] {} released {} quest {} intent ({})", bot->GetName(),
                  IntentTypeName(state.intent.type), state.intent.questId, reason);
        state.intent.Clear();
        ReleaseStrategyOwnership(bot, botAI, state);
    }

    void AcquireIntent(Player* bot, PlayerbotAI* botAI, LazyBotState& state,
                       LazyQuestCandidate const& candidate, uint32 nowMs)
    {
        state.intent.questId = candidate.questId;
        state.intent.type = candidate.type;
        state.intent.startedAtMs = nowMs;
        state.intent.lastProgressAtMs = nowMs;
        state.intent.lastInteractionAtMs = 0;
        state.intent.interactionFailures = 0;
        state.intent.progressFingerprint = GetQuestProgressFingerprint(bot, candidate.questId);
        state.intent.lastDistance = candidate.distance;
        state.intent.destination = candidate.destination;
        state.intent.point = candidate.point;
        state.consecutiveDiscoveryMisses = 0;

        AcquireStrategyOwnership(bot, botAI, state);

        Quest const* quest = candidate.destination->GetQuestTemplate();
        LOG_DEBUG("playerbots", "[LQ] {} acquired {} quest {} [{}] intent at {:.0f}y", bot->GetName(),
                  IntentTypeName(candidate.type), candidate.questId, quest ? quest->GetTitle() : "<unknown>",
                  candidate.distance);
    }

    void MaintainIntent(Player* bot, LazyBotState& state, TravelTarget* current)
    {
        if (!current || current->isGroupCopy())
            return;

        if (current->getDestination() == state.intent.destination)
        {
            bool const relationIntent = state.intent.type != LazyQuestIntentType::DoQuest;
            if (relationIntent)
                current->setForced(true);

            if (current->getStatus() != TRAVEL_STATUS_EXPIRED && current->isActive())
                return;
        }

        if (current->isForced())
            return;

        current->setTarget(state.intent.destination, state.intent.point);
        current->setForced(state.intent.type != LazyQuestIntentType::DoQuest);
        LOG_DEBUG("playerbots", "[LQ] {} restored {} quest {} travel target", bot->GetName(),
                  IntentTypeName(state.intent.type), state.intent.questId);
    }

    ObjectGuid FindNearbyQuestGiver(Player* bot, PlayerbotAI* botAI,
                                    QuestRelationTravelDestination* destination)
    {
        if (!destination)
            return ObjectGuid::Empty;

        int32 const entry = destination->getEntry();
        WorldObject* nearest = nullptr;

        if (entry > 0)
        {
            GuidVector const npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
            for (ObjectGuid const& guid : npcs)
            {
                Creature* creature = botAI->GetCreature(guid);
                if (!creature || creature->GetEntry() != static_cast<uint32>(entry) ||
                    !bot->CanInteractWithQuestGiver(creature))
                    continue;

                if (!nearest || bot->GetDistance(creature) < bot->GetDistance(nearest))
                    nearest = creature;
            }
        }
        else if (entry < 0)
        {
            GuidVector const gameObjects =
                botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects")->Get();
            uint32 const gameObjectEntry = static_cast<uint32>(-entry);
            for (ObjectGuid const& guid : gameObjects)
            {
                GameObject* gameObject = botAI->GetGameObject(guid);
                if (!gameObject || gameObject->GetEntry() != gameObjectEntry ||
                    !bot->CanInteractWithQuestGiver(gameObject))
                    continue;

                if (!nearest || bot->GetDistance(gameObject) < bot->GetDistance(nearest))
                    nearest = gameObject;
            }
        }

        return nearest ? nearest->GetGUID() : ObjectGuid::Empty;
    }

    uint32 GetInteractionRetryMs(LazyQuestIntent const& intent)
    {
        uint32 const shift = std::min<uint32>(intent.interactionFailures, 4);
        return std::min<uint32>(QUEST_INTERACTION_RETRY_MIN_MS << shift, QUEST_INTERACTION_RETRY_MAX_MS);
    }

    bool TryQuestInteraction(Player* bot, PlayerbotAI* botAI, LazyQuestIntent& intent,
                             TravelTarget* current, uint32 nowMs)
    {
        if (intent.type == LazyQuestIntentType::DoQuest || !current ||
            current->getDestination() != intent.destination ||
            (intent.lastInteractionAtMs != 0 &&
             getMSTimeDiff(intent.lastInteractionAtMs, nowMs) < GetInteractionRetryMs(intent)))
            return false;

        auto* relation = dynamic_cast<QuestRelationTravelDestination*>(intent.destination);
        ObjectGuid const questGiver = FindNearbyQuestGiver(bot, botAI, relation);
        if (!questGiver)
            return false;

        intent.lastInteractionAtMs = nowMs;
        bool completed = false;

        if (intent.type == LazyQuestIntentType::PickUp)
        {
            Quest const* quest = intent.destination->GetQuestTemplate();
            if (!quest || !IsLazyQuestDestinationActive(bot, intent.destination, intent.type))
                return false;

            WorldPacket packet(CMSG_QUESTGIVER_ACCEPT_QUEST);
            packet << questGiver << intent.questId << uint32(0);
            packet.rpos(0);
            bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(packet);
            completed = bot->GetQuestStatus(intent.questId) != QUEST_STATUS_NONE;
        }
        else
        {
            WorldPacket packet(CMSG_QUESTGIVER_COMPLETE_QUEST);
            packet << questGiver;
            packet.rpos(0);
            completed = botAI->DoSpecificAction("talk to quest giver", Event("lazy questing", packet), true);
        }

        if (completed)
            intent.interactionFailures = 0;
        else if (intent.interactionFailures < 255)
            ++intent.interactionFailures;

        LOG_DEBUG("playerbots", "[LQ] {} {} {} quest {} interaction", bot->GetName(),
                  completed ? "completed" : "attempted", IntentTypeName(intent.type), intent.questId);
        return completed;
    }

    void UpdateIntentProgress(Player* bot, LazyQuestIntent& intent, uint32 nowMs)
    {
        uint64 const fingerprint = GetQuestProgressFingerprint(bot, intent.questId);
        WorldPosition botPosition(bot);
        float const distance = intent.point->distance(&botPosition);

        if (fingerprint != intent.progressFingerprint || distance + TRAVEL_PROGRESS_YARDS < intent.lastDistance)
        {
            intent.progressFingerprint = fingerprint;
            intent.lastDistance = distance;
            intent.lastProgressAtMs = nowMs;
        }
    }

    void AddQuestCooldown(LazyBotState& state, uint32 questId, SchedulerTime now)
    {
        if (!state.questRetryAfter.count(questId) && state.questRetryAfter.size() >= MAX_QUEST_COOLDOWNS_PER_BOT)
        {
            auto const earliest = std::min_element(
                state.questRetryAfter.begin(), state.questRetryAfter.end(),
                [](auto const& left, auto const& right) { return left.second < right.second; });
            if (earliest != state.questRetryAfter.end())
                state.questRetryAfter.erase(earliest);
        }

        state.questRetryAfter[questId] = now + std::chrono::milliseconds(FAILED_QUEST_COOLDOWN_MS);
    }

    std::unordered_set<uint32> GetCoolingDownQuests(LazyBotState& state, SchedulerTime now)
    {
        std::unordered_set<uint32> coolingDown;
        coolingDown.reserve(state.questRetryAfter.size());

        for (auto itr = state.questRetryAfter.begin(); itr != state.questRetryAfter.end();)
        {
            if (now >= itr->second)
            {
                itr = state.questRetryAfter.erase(itr);
                continue;
            }

            coolingDown.insert(itr->first);
            ++itr;
        }

        return coolingDown;
    }

    struct ScheduledBot
    {
        SchedulerTime due;
        uint64 guid = 0;
        uint64 generation = 0;
    };

    struct ScheduledBotLater
    {
        bool operator()(ScheduledBot const& left, ScheduledBot const& right) const
        {
            return left.due > right.due;
        }
    };

    enum class PendingBotEventType : uint8
    {
        Register,
        Wake,
        Remove,
    };

    struct PendingBotEvent
    {
        PendingBotEventType type;
        uint64 guid;
        uint8 attempts = 0;
    };

    struct SchedulerMetrics
    {
        uint64 activeProcessed = 0;
        uint64 discoveryProcessed = 0;
        uint64 discoveryMicros = 0;
        uint64 discoveryMaxMicros = 0;
        uint64 selectorRuns = 0;
        uint64 indexedPointsVisited = 0;
        uint64 pickupDestinationsEvaluated = 0;
        uint64 candidatesFound = 0;
        uint64 budgetLimitedTicks = 0;
    };

    class LazyQuestingScheduler
    {
    public:
        static LazyQuestingScheduler& Instance()
        {
            static LazyQuestingScheduler scheduler;
            return scheduler;
        }

        void Configure(LazyQuestingConfig const& config)
        {
            bool const wasEnabled = _config.enabled;
            _config = config;

            if (wasEnabled && !_config.enabled)
                _clearRequested = true;
            else if (!wasEnabled && _config.enabled)
                _registerExistingRequested = true;
        }

        void Start()
        {
            _registerExistingRequested = true;
            _nextMetricsAt = SchedulerClock::now() + std::chrono::milliseconds(_config.metricsIntervalMs);
        }

        void QueueEvent(PendingBotEventType type, Player* player)
        {
            if (!player || (type != PendingBotEventType::Remove && !IsLazyQuestingBot(player)))
                return;

            std::lock_guard<std::mutex> lock(_pendingEventsMutex);
            _pendingEvents.push_back({ type, player->GetGUID().GetRawValue(), 0 });
        }

        void Update()
        {
            SchedulerTime const tickStart = SchedulerClock::now();

            if (_clearRequested)
                ClearStates();

            DrainPendingEvents(tickStart);
            if (!_config.enabled)
                return;

            if (_registerExistingRequested)
                RegisterExistingBots(tickStart);

            EnsureIndex(tickStart);
            if (!IsLazyQuestIndexReady())
            {
                LogMetricsIfDue(tickStart);
                return;
            }

            auto const budget = std::chrono::milliseconds(_config.worldBudgetMs);
            SchedulerTime const deadline = tickStart + budget;
            SchedulerTime const activeSliceDeadline = tickStart + budget * 3 / 4;
            uint32 activeProcessed = 0;
            uint32 discoveryProcessed = 0;

            ProcessActiveQueue(activeSliceDeadline, activeProcessed);
            ProcessDiscoveryQueue(deadline, discoveryProcessed);
            ProcessActiveQueue(deadline, activeProcessed);

            if (SchedulerClock::now() >= deadline)
                ++_metrics.budgetLimitedTicks;

            LogMetricsIfDue(SchedulerClock::now());
        }

        void Shutdown()
        {
            ClearStates();
        }

    private:
        using ScheduleQueue = std::priority_queue<ScheduledBot, std::vector<ScheduledBot>, ScheduledBotLater>;

        LazyQuestingScheduler() = default;

        uint32 GetGuidJitter(uint64 guid, uint32 rangeMs) const
        {
            if (!rangeMs)
                return 0;

            guid ^= guid >> 33;
            guid *= 0xff51afd7ed558ccdULL;
            guid ^= guid >> 33;
            guid *= 0xc4ceb9fe1a85ec53ULL;
            guid ^= guid >> 33;
            return static_cast<uint32>(guid % rangeMs);
        }

        void Schedule(uint64 guid, LazyBotState& state, ScheduleLane lane, SchedulerTime due)
        {
            state.scheduledLane = lane;
            ++state.scheduleGeneration;
            ScheduledBot entry{ due, guid, state.scheduleGeneration };

            if (lane == ScheduleLane::Active)
                _activeQueue.push(entry);
            else
                _discoveryQueue.push(entry);
        }

        bool RegisterBot(Player* player, SchedulerTime now, bool immediate)
        {
            if (!player)
                return false;

            // Non-strict players are intentionally ignored and must not consume registration retries.
            if (!IsLazyQuestingBot(player))
                return true;

            if (!GET_PLAYERBOT_AI(player))
                return false;

            uint64 const guid = player->GetGUID().GetRawValue();
            auto const inserted = _states.try_emplace(guid);
            if (!inserted.second)
                return true;

            uint32 const jitter = immediate ? 0 : GetGuidJitter(guid, _config.discoveryIntervalMs);
            Schedule(guid, inserted.first->second, ScheduleLane::Discovery,
                     now + std::chrono::milliseconds(jitter));
            return true;
        }

        void WakeBot(Player* player, SchedulerTime now)
        {
            if (!player)
                return;

            uint64 const guid = player->GetGUID().GetRawValue();
            if (!IsLazyQuestingBot(player))
            {
                RemoveBot(guid);
                return;
            }

            if (!GET_PLAYERBOT_AI(player))
                return;

            auto const inserted = _states.try_emplace(guid);
            LazyBotState& state = inserted.first->second;
            state.consecutiveDiscoveryMisses = 0;
            Schedule(guid, state, state.intent.IsActive() ? ScheduleLane::Active : ScheduleLane::Discovery, now);
        }

        void RemoveBot(uint64 guid)
        {
            auto const stateItr = _states.find(guid);
            if (stateItr == _states.end())
                return;

            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(guid));
            PlayerbotAI* botAI = player ? GET_PLAYERBOT_AI(player) : nullptr;
            if (player && botAI)
                ReleaseIntent(player, botAI, stateItr->second, "bot logged out");

            _states.erase(stateItr);
        }

        void DrainPendingEvents(SchedulerTime now)
        {
            std::vector<PendingBotEvent> events;
            std::vector<PendingBotEvent> retries;
            {
                std::lock_guard<std::mutex> lock(_pendingEventsMutex);
                std::size_t const count = std::min(_pendingEvents.size(), MAX_PENDING_EVENTS_PER_TICK);
                events.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    events.push_back(_pendingEvents.front());
                    _pendingEvents.pop_front();
                }
            }

            if (!_config.enabled)
                return;

            for (PendingBotEvent const& event : events)
            {
                if (event.type == PendingBotEventType::Remove)
                {
                    RemoveBot(event.guid);
                    continue;
                }

                Player* player = ObjectAccessor::FindPlayer(ObjectGuid(event.guid));
                if (event.type == PendingBotEventType::Register)
                {
                    if (!RegisterBot(player, now, false) && event.attempts < MAX_REGISTRATION_RETRIES)
                        retries.push_back({ event.type, event.guid, static_cast<uint8>(event.attempts + 1) });
                }
                else
                    WakeBot(player, now);
            }

            if (!retries.empty())
            {
                std::lock_guard<std::mutex> lock(_pendingEventsMutex);
                _pendingEvents.insert(_pendingEvents.end(), retries.begin(), retries.end());
            }
        }

        void RegisterExistingBots(SchedulerTime now)
        {
            _registerExistingRequested = false;
            std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
            HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();
            for (auto const& player : players)
                RegisterBot(player.second, now, false);
        }

        void EnsureIndex(SchedulerTime now)
        {
            if (IsLazyQuestIndexReady() || now < _nextIndexAttempt)
                return;

            if (InitializeLazyQuestIndex())
            {
                LazyQuestIndexStats const stats = GetLazyQuestIndexStats();
                LOG_INFO("server.loading", "mod-lazy-questing indexed {} quest-giver points in {} spatial cells.",
                         stats.points, stats.cells);
                return;
            }

            _nextIndexAttempt = now + std::chrono::seconds(30);
            LOG_WARN("server.loading", "mod-lazy-questing is waiting for world quest and spawn data.");
        }

        bool TakeDue(ScheduleQueue& queue, ScheduleLane lane, SchedulerTime now, ScheduledBot& result)
        {
            while (!queue.empty())
            {
                ScheduledBot const entry = queue.top();
                if (entry.due > now)
                    return false;

                queue.pop();
                auto const state = _states.find(entry.guid);
                if (state == _states.end() || state->second.scheduleGeneration != entry.generation ||
                    state->second.scheduledLane != lane)
                    continue;

                result = entry;
                return true;
            }

            return false;
        }

        bool GetProcessableBot(uint64 guid, Player*& player, PlayerbotAI*& botAI)
        {
            player = ObjectAccessor::FindPlayer(ObjectGuid(guid));
            botAI = player ? GET_PLAYERBOT_AI(player) : nullptr;

            if (!player || !botAI)
            {
                _states.erase(guid);
                return false;
            }

            if (!IsLazyQuestingBot(player))
            {
                RemoveBot(guid);
                player = nullptr;
                botAI = nullptr;
                return false;
            }

            return true;
        }

        bool IsTemporarilyUnavailable(Player* player) const
        {
            return !player->IsInWorld() || !player->IsAlive() || player->IsInCombat() ||
                   player->IsBeingTeleported();
        }

        void ProcessActiveQueue(SchedulerTime deadline, uint32& processed)
        {
            while (processed < _config.maxActiveBotsPerTick && SchedulerClock::now() < deadline)
            {
                ScheduledBot entry;
                if (!TakeDue(_activeQueue, ScheduleLane::Active, SchedulerClock::now(), entry))
                    return;

                ProcessActiveBot(entry.guid, SchedulerClock::now());
                ++processed;
                ++_metrics.activeProcessed;
            }
        }

        void ProcessDiscoveryQueue(SchedulerTime deadline, uint32& processed)
        {
            while (processed < _config.maxDiscoveryBotsPerTick && SchedulerClock::now() < deadline)
            {
                ScheduledBot entry;
                if (!TakeDue(_discoveryQueue, ScheduleLane::Discovery, SchedulerClock::now(), entry))
                    return;

                ProcessDiscoveryBot(entry.guid, SchedulerClock::now());
                ++processed;
                ++_metrics.discoveryProcessed;
            }
        }

        void ProcessActiveBot(uint64 guid, SchedulerTime now)
        {
            auto const stateItr = _states.find(guid);
            if (stateItr == _states.end())
                return;

            LazyBotState& state = stateItr->second;
            Player* player = nullptr;
            PlayerbotAI* botAI = nullptr;
            if (!GetProcessableBot(guid, player, botAI))
                return;

            if (!state.intent.IsActive())
            {
                Schedule(guid, state, ScheduleLane::Discovery, now);
                return;
            }

            if (IsTemporarilyUnavailable(player))
            {
                Schedule(guid, state, ScheduleLane::Active,
                         now + std::chrono::milliseconds(TRANSIENT_RETRY_MS));
                return;
            }

            uint32 const nowMs = getMSTime();
            TravelTarget* current = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

            if (!IsIntentValid(player, state.intent))
            {
                ReleaseIntent(player, botAI, state, "quest state changed");
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(POST_INTERACTION_RETRY_MS));
                return;
            }

            UpdateIntentProgress(player, state.intent, nowMs);
            if (getMSTimeDiff(state.intent.lastProgressAtMs, nowMs) >= NO_PROGRESS_TIMEOUT_MS)
            {
                uint32 const stalledQuestId = state.intent.questId;
                AddQuestCooldown(state, stalledQuestId, now);
                LOG_WARN("playerbots", "[LQ] {} cooling down stalled quest {} for 20 minutes",
                         player->GetName(), stalledQuestId);
                ReleaseIntent(player, botAI, state, "no progress for 5 minutes");
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(_config.discoveryIntervalMs));
                return;
            }

            MaintainIntent(player, state, current);
            bool const interacted = TryQuestInteraction(player, botAI, state.intent, current, nowMs);
            Schedule(guid, state, ScheduleLane::Active,
                     now + std::chrono::milliseconds(interacted ? POST_INTERACTION_RETRY_MS
                                                                : _config.activeCheckIntervalMs));
        }

        uint32 GetDiscoveryDelayMs(uint64 guid, LazyBotState& state) const
        {
            uint32 delay = _config.discoveryIntervalMs;
            uint8 const shifts = state.consecutiveDiscoveryMisses
                ? std::min<uint8>(state.consecutiveDiscoveryMisses - 1, 4)
                : 0;
            for (uint8 i = 0; i < shifts && delay < _config.maxDiscoveryBackoffMs; ++i)
                delay = std::min(delay * 2, _config.maxDiscoveryBackoffMs);

            uint32 const jitterRange = std::max<uint32>(1, _config.discoveryIntervalMs / 4);
            return std::min(delay + GetGuidJitter(guid + state.scheduleGeneration, jitterRange),
                            _config.maxDiscoveryBackoffMs);
        }

        void ProcessDiscoveryBot(uint64 guid, SchedulerTime now)
        {
            auto const stateItr = _states.find(guid);
            if (stateItr == _states.end())
                return;

            LazyBotState& state = stateItr->second;
            Player* player = nullptr;
            PlayerbotAI* botAI = nullptr;
            if (!GetProcessableBot(guid, player, botAI))
                return;

            if (state.intent.IsActive())
            {
                Schedule(guid, state, ScheduleLane::Active, now);
                return;
            }

            if (IsTemporarilyUnavailable(player))
            {
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(TRANSIENT_RETRY_MS));
                return;
            }

            TravelTarget* current = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
            if (!ShouldNudge(player, botAI, current))
            {
                state.consecutiveDiscoveryMisses = 0;
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(GetDiscoveryDelayMs(guid, state)));
                return;
            }

            std::unordered_set<uint32> const coolingDown = GetCoolingDownQuests(state, now);
            LazyQuestCandidate candidate;
            LazyQuestSelectionStats selectionStats;
            SchedulerTime const selectionStart = SchedulerClock::now();
            bool const found = FindLazyQuestCandidate(player, candidate, &coolingDown, &selectionStats);
            uint64 const selectionMicros = static_cast<uint64>(std::chrono::duration_cast<std::chrono::microseconds>(
                SchedulerClock::now() - selectionStart).count());

            _metrics.discoveryMicros += selectionMicros;
            _metrics.discoveryMaxMicros = std::max(_metrics.discoveryMaxMicros, selectionMicros);
            ++_metrics.selectorRuns;
            _metrics.indexedPointsVisited += selectionStats.indexedPointsVisited;
            _metrics.pickupDestinationsEvaluated += selectionStats.pickupDestinationsEvaluated;
            _metrics.candidatesFound += selectionStats.candidatesFound;

            if (!found || !current)
            {
                if (state.consecutiveDiscoveryMisses < 255)
                    ++state.consecutiveDiscoveryMisses;
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(GetDiscoveryDelayMs(guid, state)));
                return;
            }

            AcquireIntent(player, botAI, state, candidate, getMSTime());
            current->setTarget(candidate.destination, candidate.point);
            current->setForced(candidate.type != LazyQuestIntentType::DoQuest);
            Schedule(guid, state, ScheduleLane::Active,
                     now + std::chrono::milliseconds(_config.activeCheckIntervalMs));
        }

        void LogMetricsIfDue(SchedulerTime now)
        {
            if (now < _nextMetricsAt)
                return;

            std::size_t activeIntents = 0;
            for (auto const& state : _states)
            {
                if (state.second.intent.IsActive())
                    ++activeIntents;
            }

            double const averageDiscoveryMicros = _metrics.selectorRuns
                ? static_cast<double>(_metrics.discoveryMicros) / _metrics.selectorRuns
                : 0.0;

            LOG_INFO("playerbots",
                     "[LQ] scheduler: bots={}, intents={}, queues={}/{}, processed={}/{}, "
                     "selector avg/max={:.0f}/{}us, indexed/evaluated/candidates={}/{}/{}, budget-limited ticks={}",
                     _states.size(), activeIntents, _activeQueue.size(), _discoveryQueue.size(),
                     _metrics.activeProcessed, _metrics.discoveryProcessed, averageDiscoveryMicros,
                     _metrics.discoveryMaxMicros, _metrics.indexedPointsVisited,
                     _metrics.pickupDestinationsEvaluated, _metrics.candidatesFound,
                     _metrics.budgetLimitedTicks);

            _metrics = {};
            _nextMetricsAt = now + std::chrono::milliseconds(_config.metricsIntervalMs);
        }

        void ClearStates()
        {
            for (auto& state : _states)
            {
                Player* player = ObjectAccessor::FindPlayer(ObjectGuid(state.first));
                PlayerbotAI* botAI = player ? GET_PLAYERBOT_AI(player) : nullptr;
                if (player && botAI)
                    ReleaseIntent(player, botAI, state.second, "scheduler stopped");
            }

            _states.clear();
            _activeQueue = {};
            _discoveryQueue = {};
            _clearRequested = false;
        }

        LazyQuestingConfig _config;
        std::unordered_map<uint64, LazyBotState> _states;
        ScheduleQueue _activeQueue;
        ScheduleQueue _discoveryQueue;
        std::mutex _pendingEventsMutex;
        std::deque<PendingBotEvent> _pendingEvents;
        SchedulerMetrics _metrics;
        SchedulerTime _nextIndexAttempt = SchedulerTime::min();
        SchedulerTime _nextMetricsAt = SchedulerTime::max();
        bool _clearRequested = false;
        bool _registerExistingRequested = false;
    };
}

class LazyQuestingPlayerScript final : public PlayerScript
{
public:
    LazyQuestingPlayerScript()
        : PlayerScript("LazyQuestingPlayerScript",
                       { PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST, PLAYERHOOK_ON_LEVEL_CHANGED,
                         PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_BEFORE_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Register, player);
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Remove, player);
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* /*quest*/) override
    {
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Wake, player);
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Wake, player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Wake, player);
    }
};

class LazyQuestingWorldScript final : public WorldScript
{
public:
    LazyQuestingWorldScript()
        : WorldScript("LazyQuestingWorldScript",
                      { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_STARTUP,
                        WORLDHOOK_ON_SHUTDOWN })
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LazyQuestingConfig const config = ReadLazyQuestingConfig();
        LazyQuestingScheduler::Instance().Configure(config);
        LOG_INFO("server.loading",
                 "mod-lazy-questing config: enabled={}, budget={}ms, active={}ms, discovery={}..{}ms, "
                 "per-tick active/discovery={}/{}.",
                 config.enabled, config.worldBudgetMs, config.activeCheckIntervalMs,
                 config.discoveryIntervalMs, config.maxDiscoveryBackoffMs,
                 config.maxActiveBotsPerTick, config.maxDiscoveryBotsPerTick);
    }

    void OnStartup() override
    {
        if (InitializeLazyQuestIndex())
        {
            LazyQuestIndexStats const stats = GetLazyQuestIndexStats();
            LOG_INFO("server.loading", "mod-lazy-questing indexed {} quest-giver points in {} spatial cells.",
                     stats.points, stats.cells);
        }
        LazyQuestingScheduler::Instance().Start();
        LOG_INFO("server.loading", "mod-lazy-questing loaded.");
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        LazyQuestingScheduler::Instance().Update();
    }

    void OnShutdown() override
    {
        LazyQuestingScheduler::Instance().Shutdown();
    }
};

void Addmod_lazy_questingScripts()
{
    new LazyQuestingWorldScript();
    new LazyQuestingPlayerScript();
}
