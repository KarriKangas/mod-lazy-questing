#include "Config.h"
#include "Event.h"
#include "Item.h"
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
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LazyQuestSelector.h"

namespace
{
    using SchedulerClock = std::chrono::steady_clock;
    using SchedulerTime = SchedulerClock::time_point;

    enum class LazyQuestExperimentMode : uint8
    {
        Control,
        AssistOnly,
        Current,
    };

    constexpr uint32 NO_MOVEMENT_TIMEOUT_MS = MINUTE * IN_MILLISECONDS;
    constexpr uint32 NO_APPROACH_TIMEOUT_MS = 3 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 NO_WORK_PROGRESS_TIMEOUT_MS = 2 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 HARD_QUEST_TIMEOUT_MS = 10 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 EXHAUSTED_QUEST_COOLDOWN_MS = 5 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 FAILED_QUEST_COOLDOWN_MS = 20 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 QUEST_INTERACTION_RETRY_MIN_MS = 5 * IN_MILLISECONDS;
    constexpr uint32 QUEST_INTERACTION_RETRY_MAX_MS = MINUTE * IN_MILLISECONDS;
    constexpr uint32 TRANSIENT_RETRY_MS = 5 * IN_MILLISECONDS;
    constexpr uint32 POST_INTERACTION_RETRY_MS = IN_MILLISECONDS;
    constexpr uint32 REGISTRATION_RETRY_MIN_MS = 250;
    constexpr uint32 REGISTRATION_RETRY_MAX_MS = 5 * IN_MILLISECONDS;
    constexpr uint32 REGISTRATION_RETRY_TIMEOUT_MS = MINUTE * IN_MILLISECONDS;
    constexpr uint32 ROSTER_RECONCILE_INTERVAL_MS = 30 * IN_MILLISECONDS;
    constexpr std::size_t MAX_PENDING_EVENTS_PER_TICK = 512;
    constexpr uint32 MAX_REGISTRATIONS_PER_TICK = 16;
    constexpr uint32 MAX_ROSTER_RECONCILE_PER_TICK = 32;
    constexpr std::size_t MAX_QUEST_COOLDOWNS_PER_BOT = 32;
    constexpr std::size_t MAX_FAILED_POINTS_PER_INTENT = 6;
    constexpr uint8 MAX_FAILED_LEGS_PER_INTENT = 3;
    constexpr float MOVEMENT_PROGRESS_YARDS = 5.0f;
    constexpr float APPROACH_PROGRESS_YARDS = 10.0f;
    constexpr float CONSERVATIVE_TURN_IN_DISTANCE = 250.0f;
    constexpr float CONSERVATIVE_PICKUP_DISTANCE = 200.0f;
    constexpr uint32 CONSERVATIVE_INTENT_LEASE_MS = MINUTE * IN_MILLISECONDS;
    constexpr uint32 CONSERVATIVE_NO_PROGRESS_MS = 30 * IN_MILLISECONDS;
    constexpr uint32 CONSERVATIVE_QUEST_COOLDOWN_MS = 2 * MINUTE * IN_MILLISECONDS;

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
        std::string experimentRunId = "unassigned";
        uint32 experimentSeed = 1;
        uint32 experimentControlPercent = 0;
        uint32 experimentAssistOnlyPercent = 0;
        bool flightRecorderEnabled = true;
        uint32 flightRecorderSampleIntervalMs = 5 * IN_MILLISECONDS;
        uint32 flightRecorderMetricsIntervalMs = MINUTE * IN_MILLISECONDS;
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
        config.experimentRunId = sConfigMgr->GetOption<std::string>(
            "LazyQuesting.Experiment.RunId", "unassigned");
        config.experimentSeed = sConfigMgr->GetOption<uint32>("LazyQuesting.Experiment.Seed", 1);
        config.experimentControlPercent = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.Experiment.ControlPercent", 0), 0, 100);
        config.experimentAssistOnlyPercent = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.Experiment.AssistOnlyPercent", 0), 0,
            100 - config.experimentControlPercent);
        config.flightRecorderEnabled = sConfigMgr->GetOption<bool>("LazyQuesting.FlightRecorder.Enable", true);
        config.flightRecorderSampleIntervalMs = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.FlightRecorder.SampleIntervalMs", 5000), 1000, 60000);
        config.flightRecorderMetricsIntervalMs = ClampConfig(
            sConfigMgr->GetOption<uint32>("LazyQuesting.FlightRecorder.MetricsIntervalMs", 60000), 10000, 3600000);
        return config;
    }

    char const* ExperimentModeName(LazyQuestExperimentMode mode)
    {
        switch (mode)
        {
            case LazyQuestExperimentMode::Control:
                return "control";
            case LazyQuestExperimentMode::AssistOnly:
                return "assist-only";
            case LazyQuestExperimentMode::Current:
            default:
                return "current";
        }
    }

    uint32 GetExperimentBucket(Player const* player, uint32 seed)
    {
        uint64 value = player->GetGUID().GetCounter();
        value ^= static_cast<uint64>(player->getRace()) << 48;
        value ^= static_cast<uint64>(player->getClass()) << 56;
        value ^= static_cast<uint64>(seed) * 0x9e3779b97f4a7c15ULL;
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33;
        value *= 0xc4ceb9fe1a85ec53ULL;
        value ^= value >> 33;
        return static_cast<uint32>(value % 100);
    }

    LazyQuestExperimentMode GetExperimentMode(Player const* player, LazyQuestingConfig const& config)
    {
        if (!config.enabled)
            return LazyQuestExperimentMode::Control;

        uint32 const bucket = GetExperimentBucket(player, config.experimentSeed);
        if (bucket < config.experimentControlPercent)
            return LazyQuestExperimentMode::Control;
        if (bucket < config.experimentControlPercent + config.experimentAssistOnlyPercent)
            return LazyQuestExperimentMode::AssistOnly;
        return LazyQuestExperimentMode::Current;
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

    constexpr std::size_t INTENT_TYPE_COUNT = 3;

    std::size_t IntentTypeIndex(LazyQuestIntentType type)
    {
        return static_cast<std::size_t>(type);
    }

    enum class PreviousTargetType : uint8
    {
        None,
        Null,
        Grind,
        Explore,
        Rpg,
        Quest,
        Other,
        Count,
    };

    constexpr std::size_t PREVIOUS_TARGET_TYPE_COUNT = static_cast<std::size_t>(PreviousTargetType::Count);

    struct LazyQuestIntent
    {
        uint32 questId = 0;
        LazyQuestIntentType type = LazyQuestIntentType::DoQuest;
        uint32 startedAtMs = 0;
        uint32 lastSampleAtMs = 0;
        uint32 lastInteractionAtMs = 0;
        uint32 noMovementMs = 0;
        uint32 noApproachMs = 0;
        uint32 noWorkProgressMs = 0;
        uint32 noQuestProgressMs = 0;
        float initialDistance = 0.0f;
        float maxDistance = 2500.0f;
        PreviousTargetType previousTargetType = PreviousTargetType::None;
        uint8 interactionFailures = 0;
        uint8 legFailures = 0;
        uint64 progressFingerprint = 0;
        uint32 lastMapId = 0;
        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastZ = 0.0f;
        float bestDistance = 0.0f;
        TravelDestination* destination = nullptr;
        WorldPosition* point = nullptr;
        std::vector<WorldPosition*> failedPoints;
        bool suspended = false;

        bool IsActive() const { return questId != 0 && destination && point; }

        void Clear()
        {
            questId = 0;
            type = LazyQuestIntentType::DoQuest;
            startedAtMs = 0;
            lastSampleAtMs = 0;
            lastInteractionAtMs = 0;
            noMovementMs = 0;
            noApproachMs = 0;
            noWorkProgressMs = 0;
            noQuestProgressMs = 0;
            initialDistance = 0.0f;
            maxDistance = 2500.0f;
            previousTargetType = PreviousTargetType::None;
            interactionFailures = 0;
            legFailures = 0;
            progressFingerprint = 0;
            lastMapId = 0;
            lastX = 0.0f;
            lastY = 0.0f;
            lastZ = 0.0f;
            bestDistance = 0.0f;
            destination = nullptr;
            point = nullptr;
            failedPoints.clear();
            suspended = false;
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
        LazyQuestExperimentMode experimentMode = LazyQuestExperimentMode::Current;
        uint64 scheduleGeneration = 0;
        ScheduleLane scheduledLane = ScheduleLane::Discovery;
        uint8 consecutiveDiscoveryMisses = 0;
        bool strategyOwnershipActive = false;
        bool addedTravelStrategy = false;
        bool removedNewRpgStrategy = false;
    };

    PreviousTargetType ClassifyPreviousTarget(TravelTarget* target)
    {
        if (!target || !target->getDestination())
            return PreviousTargetType::None;

        TravelDestination* destination = target->getDestination();
        if (dynamic_cast<NullTravelDestination*>(destination))
            return PreviousTargetType::Null;
        if (dynamic_cast<GrindTravelDestination*>(destination))
            return PreviousTargetType::Grind;
        if (dynamic_cast<ExploreTravelDestination*>(destination))
            return PreviousTargetType::Explore;
        if (dynamic_cast<RpgTravelDestination*>(destination))
            return PreviousTargetType::Rpg;
        if (dynamic_cast<QuestTravelDestination*>(destination))
            return PreviousTargetType::Quest;
        return PreviousTargetType::Other;
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

    bool HasAvailableLoot(PlayerbotAI* botAI)
    {
        AiObjectContext* context = botAI->GetAiObjectContext();
        Value<bool>* value = context->GetValue<bool>("has available loot");
        return value && value->Get();
    }

    bool HasProtectedRpgActivity(PlayerbotAI* botAI)
    {
        return botAI->rpgInfo.GetStatus() != RPG_IDLE;
    }

    bool HasProtectedActivity(PlayerbotAI* botAI)
    {
        return HasAvailableLoot(botAI) || HasEssentialRpgNeed(botAI) || HasProtectedRpgActivity(botAI);
    }

    bool ShouldNudge(Player* bot, PlayerbotAI* botAI, TravelTarget* current)
    {
        if (!current || current->isGroupCopy() || HasProtectedActivity(botAI))
            return false;

        TravelDestination* destination = current->getDestination();

        if (dynamic_cast<RpgTravelDestination*>(destination))
            return false;

        if (current->isForced())
            return false;

        if (!destination || dynamic_cast<NullTravelDestination*>(destination) ||
            dynamic_cast<ExploreTravelDestination*>(destination))
            return true;

        // Active grinding is the dominant early-level XP source in the measured control cohorts.
        // Only replace a grind target after Playerbots itself considers it inactive or expired.
        if (dynamic_cast<GrindTravelDestination*>(destination))
            return !IsUsableTarget(bot, current);

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

    bool IsIntentQuestStateValid(Player* bot, LazyQuestIntent const& intent)
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

        return currentType == intent.type;
    }

    void AcquireStrategyOwnership(Player* bot, PlayerbotAI* botAI, LazyBotState& state)
    {
        if (state.strategyOwnershipActive)
            return;

        bool const hadTravel = botAI->HasStrategy("travel", BOT_STATE_NON_COMBAT);
        bool const hadNewRpg = botAI->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);

        state.addedTravelStrategy = !hadTravel;
        state.removedNewRpgStrategy = hadNewRpg &&
            state.experimentMode != LazyQuestExperimentMode::AssistOnly;

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
                       LazyQuestCandidate const& candidate, TravelTarget* previousTarget, uint32 nowMs)
    {
        state.intent.questId = candidate.questId;
        state.intent.type = candidate.type;
        state.intent.startedAtMs = nowMs;
        state.intent.lastSampleAtMs = nowMs;
        state.intent.lastInteractionAtMs = 0;
        state.intent.noMovementMs = 0;
        state.intent.noApproachMs = 0;
        state.intent.noWorkProgressMs = 0;
        state.intent.noQuestProgressMs = 0;
        state.intent.initialDistance = candidate.distance;
        state.intent.maxDistance = state.experimentMode == LazyQuestExperimentMode::AssistOnly
            ? (candidate.type == LazyQuestIntentType::TurnIn
                ? CONSERVATIVE_TURN_IN_DISTANCE
                : CONSERVATIVE_PICKUP_DISTANCE)
            : 2500.0f;
        state.intent.previousTargetType = ClassifyPreviousTarget(previousTarget);
        state.intent.interactionFailures = 0;
        state.intent.legFailures = 0;
        state.intent.progressFingerprint = GetQuestProgressFingerprint(bot, candidate.questId);
        state.intent.lastMapId = bot->GetMapId();
        state.intent.lastX = bot->GetPositionX();
        state.intent.lastY = bot->GetPositionY();
        state.intent.lastZ = bot->GetPositionZ();
        state.intent.bestDistance = candidate.distance;
        state.intent.destination = candidate.destination;
        state.intent.point = candidate.point;
        state.intent.failedPoints.clear();
        state.intent.failedPoints.reserve(MAX_FAILED_POINTS_PER_INTENT);
        state.intent.suspended = false;
        state.consecutiveDiscoveryMisses = 0;

        AcquireStrategyOwnership(bot, botAI, state);

        Quest const* quest = candidate.destination->GetQuestTemplate();
        LOG_DEBUG("playerbots", "[LQ] {} acquired {} quest {} [{}] intent at {:.0f}y", bot->GetName(),
                  IntentTypeName(candidate.type), candidate.questId, quest ? quest->GetTitle() : "<unknown>",
                  candidate.distance);
    }

    bool IsTravelTargetForIntent(TravelTarget* current, LazyQuestIntent const& intent)
    {
        if (!current || !current->getDestination())
            return false;

        TravelDestination* destination = current->getDestination();
        Quest const* quest = destination->GetQuestTemplate();
        if (!quest || quest->GetQuestId() != intent.questId)
            return false;

        if (intent.type == LazyQuestIntentType::DoQuest)
            return dynamic_cast<QuestObjectiveTravelDestination*>(destination) != nullptr;

        auto* relation = dynamic_cast<QuestRelationTravelDestination*>(destination);
        if (!relation)
            return false;

        return intent.type == LazyQuestIntentType::PickUp ? relation->getRelation() == 0
                                                         : relation->getRelation() != 0;
    }

    void ResetIntentLegTracking(Player* bot, LazyQuestIntent& intent, uint32 nowMs)
    {
        intent.lastSampleAtMs = nowMs;
        intent.noMovementMs = 0;
        intent.noApproachMs = 0;
        intent.noWorkProgressMs = 0;
        intent.lastMapId = bot->GetMapId();
        intent.lastX = bot->GetPositionX();
        intent.lastY = bot->GetPositionY();
        intent.lastZ = bot->GetPositionZ();
        WorldPosition botPosition(bot);
        intent.bestDistance = intent.point ? intent.point->distance(&botPosition) : 0.0f;
        intent.suspended = false;
    }

    void AssignIntentLeg(Player* bot, LazyQuestIntent& intent, TravelTarget* current,
                         LazyQuestCandidate const& candidate, uint32 nowMs)
    {
        intent.destination = candidate.destination;
        intent.point = candidate.point;
        intent.lastInteractionAtMs = 0;
        intent.interactionFailures = 0;
        ResetIntentLegTracking(bot, intent, nowMs);

        current->setTarget(candidate.destination, candidate.point);
        if (intent.type != LazyQuestIntentType::DoQuest)
            current->setRadius(INTERACTION_DISTANCE);
        // Forced travel prevents an empty spawn from invalidating the destination before the bot reaches
        // the selected alternate point. Objective work becomes non-forced again on arrival.
        current->setForced(true);
    }

    void AdoptIntentLeg(Player* bot, LazyQuestIntent& intent, TravelTarget* current, uint32 nowMs)
    {
        intent.destination = current->getDestination();
        intent.point = current->getPosition();
        intent.lastInteractionAtMs = 0;
        intent.interactionFailures = 0;
        ResetIntentLegTracking(bot, intent, nowMs);
        if (intent.type != LazyQuestIntentType::DoQuest)
            current->setRadius(INTERACTION_DISTANCE);
    }

    bool ObserveQuestProgress(Player* bot, LazyQuestIntent& intent)
    {
        uint64 const fingerprint = GetQuestProgressFingerprint(bot, intent.questId);
        if (fingerprint == intent.progressFingerprint)
            return false;

        intent.progressFingerprint = fingerprint;
        intent.noQuestProgressMs = 0;
        intent.noWorkProgressMs = 0;
        intent.legFailures = 0;
        intent.failedPoints.clear();
        return true;
    }

    void PauseIntentTracking(Player* bot, LazyQuestIntent& intent, uint32 nowMs)
    {
        intent.lastSampleAtMs = nowMs;
        if (bot->IsInWorld())
        {
            intent.lastMapId = bot->GetMapId();
            intent.lastX = bot->GetPositionX();
            intent.lastY = bot->GetPositionY();
            intent.lastZ = bot->GetPositionZ();
        }
        intent.suspended = true;
    }

    void UpdateIntentProgress(Player* bot, LazyQuestIntent& intent, TravelTarget* current,
                              uint32 nowMs, bool semanticProgress)
    {
        uint32 const elapsedMs = intent.suspended ? 0 : getMSTimeDiff(intent.lastSampleAtMs, nowMs);
        intent.suspended = false;
        intent.lastSampleAtMs = nowMs;

        uint32 const currentMapId = bot->GetMapId();
        float const x = bot->GetPositionX();
        float const y = bot->GetPositionY();
        float const z = bot->GetPositionZ();
        bool resetMovementAnchor = false;

        if (currentMapId != intent.lastMapId)
        {
            intent.noMovementMs = 0;
            intent.noApproachMs = 0;
            intent.bestDistance = 0.0f;
            resetMovementAnchor = true;
        }
        else
        {
            float const dx = x - intent.lastX;
            float const dy = y - intent.lastY;
            float const dz = z - intent.lastZ;
            float const movement = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (movement >= MOVEMENT_PROGRESS_YARDS)
            {
                intent.noMovementMs = 0;
                resetMovementAnchor = true;
            }
            else
                intent.noMovementMs += elapsedMs;
        }

        WorldPosition botPosition(bot);
        float const distance = intent.point ? intent.point->distance(&botPosition) : 0.0f;
        if (intent.bestDistance == 0.0f || distance + APPROACH_PROGRESS_YARDS < intent.bestDistance)
        {
            intent.bestDistance = distance;
            intent.noApproachMs = 0;
        }
        else
            intent.noApproachMs += elapsedMs;

        if (!semanticProgress)
            intent.noQuestProgressMs += elapsedMs;

        if (current && current->getStatus() == TRAVEL_STATUS_WORK && !semanticProgress)
            intent.noWorkProgressMs += elapsedMs;
        else if (!current || current->getStatus() != TRAVEL_STATUS_WORK)
            intent.noWorkProgressMs = 0;

        intent.lastMapId = currentMapId;
        if (resetMovementAnchor)
        {
            intent.lastX = x;
            intent.lastY = y;
            intent.lastZ = z;
        }
    }

    enum class IntentRecoveryResult : uint8
    {
        Repointed,
        Exhausted,
        HardFailed,
    };

    void RememberFailedPoint(LazyQuestIntent& intent)
    {
        if (!intent.point || std::find(intent.failedPoints.begin(), intent.failedPoints.end(), intent.point) !=
            intent.failedPoints.end())
            return;

        if (intent.failedPoints.size() >= MAX_FAILED_POINTS_PER_INTENT)
            intent.failedPoints.erase(intent.failedPoints.begin());
        intent.failedPoints.push_back(intent.point);
    }

    IntentRecoveryResult RecoverIntentLeg(Player* bot, LazyQuestIntent& intent, TravelTarget* current,
                                           uint32 nowMs)
    {
        if (!current)
            return IntentRecoveryResult::Exhausted;

        RememberFailedPoint(intent);
        if (intent.legFailures < 255)
            ++intent.legFailures;

        if (intent.legFailures >= MAX_FAILED_LEGS_PER_INTENT ||
            intent.noQuestProgressMs >= HARD_QUEST_TIMEOUT_MS)
            return IntentRecoveryResult::HardFailed;

        LazyQuestCandidate candidate;
        if (!FindLazyQuestLeg(bot, intent.questId, intent.type, intent.destination,
                              intent.failedPoints, candidate, intent.maxDistance))
            return IntentRecoveryResult::Exhausted;

        AssignIntentLeg(bot, intent, current, candidate, nowMs);
        return IntentRecoveryResult::Repointed;
    }

    bool IntentNeedsRecovery(LazyQuestIntent const& intent, TravelTarget* current)
    {
        if (!current || !IsTravelTargetForIntent(current, intent))
            return true;

        TravelStatus const status = current->getStatus();
        return status == TRAVEL_STATUS_NONE || status == TRAVEL_STATUS_COOLDOWN ||
               status == TRAVEL_STATUS_EXPIRED ||
               (status == TRAVEL_STATUS_TRAVEL &&
                (intent.noMovementMs >= NO_MOVEMENT_TIMEOUT_MS ||
                 intent.noApproachMs >= NO_APPROACH_TIMEOUT_MS)) ||
               (status == TRAVEL_STATUS_WORK && intent.noWorkProgressMs >= NO_WORK_PROGRESS_TIMEOUT_MS) ||
               intent.noQuestProgressMs >= HARD_QUEST_TIMEOUT_MS;
    }

    ObjectGuid FindNearbyQuestGiver(Player* bot, QuestRelationTravelDestination* destination)
    {
        if (!destination)
            return ObjectGuid::Empty;

        int32 const entry = destination->getEntry();
        if (entry > 0)
        {
            Creature* creature = bot->FindNearestCreature(static_cast<uint32>(entry), INTERACTION_DISTANCE);
            return creature && bot->CanInteractWithQuestGiver(creature) ? creature->GetGUID() : ObjectGuid::Empty;
        }

        if (entry < 0)
        {
            uint32 const gameObjectEntry = static_cast<uint32>(-entry);
            GameObject* gameObject = bot->FindNearestGameObject(gameObjectEntry, INTERACTION_DISTANCE, true);
            return gameObject && bot->CanInteractWithQuestGiver(gameObject) ? gameObject->GetGUID()
                                                                            : ObjectGuid::Empty;
        }

        return ObjectGuid::Empty;
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
        ObjectGuid const questGiver = FindNearbyQuestGiver(bot, relation);
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
            completed = botAI->DoSpecificAction("talk to quest giver", Event("lazy questing", questGiver), true);
        }

        if (completed)
            intent.interactionFailures = 0;
        else if (intent.interactionFailures < 255)
            ++intent.interactionFailures;

        LOG_DEBUG("playerbots", "[LQ] {} {} {} quest {} interaction", bot->GetName(),
                  completed ? "completed" : "attempted", IntentTypeName(intent.type), intent.questId);
        return completed;
    }

    void AddQuestCooldown(LazyBotState& state, uint32 questId, SchedulerTime now, uint32 cooldownMs)
    {
        if (!state.questRetryAfter.count(questId) && state.questRetryAfter.size() >= MAX_QUEST_COOLDOWNS_PER_BOT)
        {
            auto const earliest = std::min_element(
                state.questRetryAfter.begin(), state.questRetryAfter.end(),
                [](auto const& left, auto const& right) { return left.second < right.second; });
            if (earliest != state.questRetryAfter.end())
                state.questRetryAfter.erase(earliest);
        }

        state.questRetryAfter[questId] = now + std::chrono::milliseconds(cooldownMs);
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
    };

    struct RegistrationRetryState
    {
        SchedulerTime startedAt;
        uint64 generation = 0;
        uint8 attempts = 0;
    };

    struct ScheduledRegistration
    {
        SchedulerTime due;
        uint64 guid = 0;
        uint64 generation = 0;
    };

    struct ScheduledRegistrationLater
    {
        bool operator()(ScheduledRegistration const& left, ScheduledRegistration const& right) const
        {
            return left.due > right.due;
        }
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
        uint64 registrationsCompleted = 0;
        uint64 registrationRetries = 0;
        uint64 registrationTimeouts = 0;
        uint64 semanticProgress = 0;
        uint64 repoints = 0;
        uint64 preemptions = 0;
        uint64 exhaustedIntents = 0;
        uint64 hardStalls = 0;
    };

    enum class IntentEndReason : uint8
    {
        Success,
        ProtectedActivity,
        ConservativeLease,
        Exhausted,
        HardStall,
        Cancelled,
        Count,
    };

    constexpr std::size_t INTENT_END_REASON_COUNT = static_cast<std::size_t>(IntentEndReason::Count);

    struct IntentMetrics
    {
        uint64 acquired = 0;
        uint64 semanticProgress = 0;
        uint64 repoints = 0;
        uint64 preemptions = 0;
        uint64 distanceYards = 0;
        uint64 maxDistanceYards = 0;
        uint64 durationMs = 0;
        std::array<uint64, INTENT_END_REASON_COUNT> endings{};
        std::array<uint64, PREVIOUS_TARGET_TYPE_COUNT> previousTargets{};

        uint64 Ended() const
        {
            uint64 total = 0;
            for (uint64 count : endings)
                total += count;
            return total;
        }

        bool HasData() const
        {
            return acquired || semanticProgress || repoints || preemptions || Ended();
        }
    };

    enum class RecordedActivity : uint8
    {
        Travelling,
        Fighting,
        Looting,
        Interacting,
        Servicing,
        Dead,
        Idle,
        Count,
    };

    constexpr std::size_t RECORDED_ACTIVITY_COUNT = static_cast<std::size_t>(RecordedActivity::Count);
    constexpr std::size_t EXPERIMENT_MODE_COUNT = 3;

    std::size_t ExperimentModeIndex(LazyQuestExperimentMode mode)
    {
        return static_cast<std::size_t>(mode);
    }

    struct FlightRecorderCounters
    {
        std::array<uint64, RECORDED_ACTIVITY_COUNT> activityMs{};
        uint64 xpKill = 0;
        uint64 xpQuest = 0;
        uint64 xpExplore = 0;
        uint64 xpOther = 0;
        uint64 kills = 0;
        uint64 deaths = 0;
        uint64 lootEvents = 0;
        uint64 questPickups = 0;
        uint64 objectiveDeltas = 0;
        uint64 questCompletions = 0;
        uint64 questTurnIns = 0;
        uint64 levelUps = 0;
        uint64 gearSamples = 0;
        uint64 sampledLevels = 0;
        uint64 sampledAverageItemLevels = 0;
        uint64 sampledTotalItemLevels = 0;
        uint64 sampledOccupiedSlots = 0;
        uint64 sampledWeaponItemLevels = 0;
        uint64 lootItems = 0;
        uint64 lootGearItems = 0;
        uint64 lootGearItemLevels = 0;
        uint64 questRewardGearItems = 0;
        uint64 questRewardGearItemLevels = 0;
        uint64 equipEvents = 0;
        uint64 equippedEventItemLevels = 0;

        void Add(FlightRecorderCounters const& other)
        {
            for (std::size_t i = 0; i < activityMs.size(); ++i)
                activityMs[i] += other.activityMs[i];

            xpKill += other.xpKill;
            xpQuest += other.xpQuest;
            xpExplore += other.xpExplore;
            xpOther += other.xpOther;
            kills += other.kills;
            deaths += other.deaths;
            lootEvents += other.lootEvents;
            questPickups += other.questPickups;
            objectiveDeltas += other.objectiveDeltas;
            questCompletions += other.questCompletions;
            questTurnIns += other.questTurnIns;
            levelUps += other.levelUps;
            gearSamples += other.gearSamples;
            sampledLevels += other.sampledLevels;
            sampledAverageItemLevels += other.sampledAverageItemLevels;
            sampledTotalItemLevels += other.sampledTotalItemLevels;
            sampledOccupiedSlots += other.sampledOccupiedSlots;
            sampledWeaponItemLevels += other.sampledWeaponItemLevels;
            lootItems += other.lootItems;
            lootGearItems += other.lootGearItems;
            lootGearItemLevels += other.lootGearItemLevels;
            questRewardGearItems += other.questRewardGearItems;
            questRewardGearItemLevels += other.questRewardGearItemLevels;
            equipEvents += other.equipEvents;
            equippedEventItemLevels += other.equippedEventItemLevels;
        }

        uint64 TotalXp() const { return xpKill + xpQuest + xpExplore + xpOther; }

        uint64 TotalActivityMs() const
        {
            uint64 total = 0;
            for (uint64 duration : activityMs)
                total += duration;
            return total;
        }

        bool HasData() const
        {
            return TotalActivityMs() || TotalXp() || kills || deaths || lootEvents || questPickups ||
                objectiveDeltas || questCompletions || questTurnIns || levelUps || gearSamples ||
                lootItems || questRewardGearItems || equipEvents;
        }
    };

    struct FlightRecorderPending
    {
        FlightRecorderCounters counters;
        std::unordered_map<uint32, uint32> completedQuestCounts;
        LazyQuestExperimentMode mode = LazyQuestExperimentMode::Current;
        SchedulerTime registeredAt = SchedulerTime::min();
        SchedulerTime removedAt = SchedulerTime::min();
        bool registered = false;
        bool removed = false;
    };

    struct FlightRecorderBotState
    {
        FlightRecorderCounters counters;
        std::unordered_map<uint32, uint32> questProgress;
        std::unordered_set<uint32> completedQuests;
        LazyQuestExperimentMode mode = LazyQuestExperimentMode::Current;
        RecordedActivity lastActivity = RecordedActivity::Idle;
        SchedulerTime lastObservedAt = SchedulerTime::min();
        uint32 currentIntentQuestId = 0;
        LazyQuestIntentType currentIntentType = LazyQuestIntentType::DoQuest;
        bool observed = false;
        bool retired = false;
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
            bool const experimentChanged = _config.experimentRunId != config.experimentRunId ||
                _config.experimentSeed != config.experimentSeed ||
                _config.experimentControlPercent != config.experimentControlPercent ||
                _config.experimentAssistOnlyPercent != config.experimentAssistOnlyPercent;
            _config = config;

            if (wasEnabled && !_config.enabled)
                _clearRequested = true;
            else if (!wasEnabled && _config.enabled)
                _rosterReconcileRequested = true;

            if (_config.enabled && experimentChanged)
            {
                _clearRequested = true;
                _rosterReconcileRequested = true;
            }
        }

        void Start()
        {
            _rosterReconcileRequested = true;
            _nextMetricsAt = SchedulerClock::now() + std::chrono::milliseconds(_config.metricsIntervalMs);
        }

        void QueueEvent(PendingBotEventType type, Player* player)
        {
            if (!player || (type != PendingBotEventType::Remove && !IsLazyQuestingBot(player)))
                return;

            std::lock_guard<std::mutex> lock(_pendingEventsMutex);
            _pendingEvents.push_back({ type, player->GetGUID().GetRawValue() });
        }

        void Update()
        {
            SchedulerTime const tickStart = SchedulerClock::now();

            if (_clearRequested)
                ClearStates();

            DrainPendingEvents(tickStart);
            if (!_config.enabled)
                return;

            ReconcileStrictRoster(tickStart);

            EnsureIndex(tickStart);
            if (!IsLazyQuestIndexReady())
            {
                LogMetricsIfDue(tickStart);
                return;
            }

            auto const budget = std::chrono::milliseconds(_config.worldBudgetMs);
            SchedulerClock::duration const preciseBudget =
                std::chrono::duration_cast<SchedulerClock::duration>(budget);
            SchedulerTime const deadline = tickStart + preciseBudget;
            SchedulerTime const registrationDeadline = tickStart + preciseBudget / 4;
            SchedulerTime const activeSliceDeadline = tickStart + preciseBudget * 3 / 4;
            uint32 registrationsProcessed = 0;
            uint32 activeProcessed = 0;
            uint32 discoveryProcessed = 0;

            ProcessRegistrationQueue(registrationDeadline, registrationsProcessed);
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

        bool GetIntentSnapshot(uint64 guid, uint32& questId, LazyQuestIntentType& type) const
        {
            auto const state = _states.find(guid);
            if (state == _states.end() || !state->second.intent.IsActive())
                return false;

            questId = state->second.intent.questId;
            type = state->second.intent.type;
            return true;
        }

    private:
        using ScheduleQueue = std::priority_queue<ScheduledBot, std::vector<ScheduledBot>, ScheduledBotLater>;
        using RegistrationQueue = std::priority_queue<ScheduledRegistration,
            std::vector<ScheduledRegistration>, ScheduledRegistrationLater>;

        LazyQuestingScheduler() = default;

        IntentMetrics& MetricsFor(LazyBotState const& state)
        {
            return _intentMetrics[ExperimentModeIndex(state.experimentMode)]
                                 [IntentTypeIndex(state.intent.type)];
        }

        void RecordIntentAcquired(LazyBotState const& state)
        {
            IntentMetrics& metrics = MetricsFor(state);
            ++metrics.acquired;
            uint64 const distance = static_cast<uint64>(std::max(0.0f, state.intent.initialDistance));
            metrics.distanceYards += distance;
            metrics.maxDistanceYards = std::max(metrics.maxDistanceYards, distance);
            ++metrics.previousTargets[static_cast<std::size_t>(state.intent.previousTargetType)];
        }

        void RecordIntentEnded(LazyBotState const& state, IntentEndReason reason, uint32 nowMs)
        {
            if (!state.intent.IsActive())
                return;

            IntentMetrics& metrics = MetricsFor(state);
            ++metrics.endings[static_cast<std::size_t>(reason)];
            metrics.durationMs += getMSTimeDiff(state.intent.startedAtMs, nowMs);
        }

        bool DidIntentSucceed(Player* player, LazyQuestIntent const& intent) const
        {
            switch (intent.type)
            {
                case LazyQuestIntentType::PickUp:
                    return player->GetQuestStatus(intent.questId) != QUEST_STATUS_NONE;
                case LazyQuestIntentType::TurnIn:
                    return player->IsQuestRewarded(intent.questId);
                case LazyQuestIntentType::DoQuest:
                default:
                    return player->GetQuestStatus(intent.questId) == QUEST_STATUS_COMPLETE;
            }
        }

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

        bool RegisterReadyBot(Player* player, SchedulerTime now, bool immediate)
        {
            if (!player || !IsLazyQuestingBot(player) || !GET_PLAYERBOT_AI(player))
                return false;

            uint64 const guid = player->GetGUID().GetRawValue();
            LazyQuestExperimentMode const experimentMode = GetExperimentMode(player, _config);
            if (experimentMode == LazyQuestExperimentMode::Control)
            {
                _controlBots.insert(guid);
                return true;
            }

            _controlBots.erase(guid);
            auto const inserted = _states.try_emplace(guid);
            if (!inserted.second)
            {
                inserted.first->second.experimentMode = experimentMode;
                return true;
            }

            inserted.first->second.experimentMode = experimentMode;
            uint32 const jitter = immediate ? 0 : GetGuidJitter(guid, _config.discoveryIntervalMs);
            Schedule(guid, inserted.first->second, ScheduleLane::Discovery,
                     now + std::chrono::milliseconds(jitter));
            ++_metrics.registrationsCompleted;
            LOG_DEBUG("playerbots", "[LQ] {} assigned to experiment mode {}", player->GetName(),
                      ExperimentModeName(experimentMode));
            return true;
        }

        void QueueRegistration(uint64 guid, SchedulerTime now)
        {
            if (_states.count(guid) || _controlBots.count(guid) || _registrationRetries.count(guid))
                return;

            RegistrationRetryState state;
            state.startedAt = now;
            state.generation = 1;
            _registrationRetries.emplace(guid, state);
            _registrationQueue.push({ now, guid, state.generation });
        }

        void RetryRegistration(uint64 guid, RegistrationRetryState& state, SchedulerTime now)
        {
            if (now - state.startedAt >= std::chrono::milliseconds(REGISTRATION_RETRY_TIMEOUT_MS))
            {
                _registrationRetries.erase(guid);
                ++_metrics.registrationTimeouts;
                return;
            }

            uint32 const shift = std::min<uint32>(state.attempts, 5);
            uint32 const delay = std::min<uint32>(REGISTRATION_RETRY_MIN_MS << shift,
                                                  REGISTRATION_RETRY_MAX_MS);
            if (state.attempts < 255)
                ++state.attempts;
            ++state.generation;
            _registrationQueue.push({ now + std::chrono::milliseconds(delay), guid, state.generation });
            ++_metrics.registrationRetries;
        }

        void ProcessRegistrationQueue(SchedulerTime deadline, uint32& processed)
        {
            while (processed < MAX_REGISTRATIONS_PER_TICK && SchedulerClock::now() < deadline &&
                   !_registrationQueue.empty())
            {
                ScheduledRegistration const entry = _registrationQueue.top();
                SchedulerTime const now = SchedulerClock::now();
                if (entry.due > now)
                    return;

                _registrationQueue.pop();
                auto retry = _registrationRetries.find(entry.guid);
                if (retry == _registrationRetries.end() || retry->second.generation != entry.generation)
                    continue;

                Player* player = ObjectAccessor::FindPlayer(ObjectGuid(entry.guid));
                if (player && !IsLazyQuestingBot(player))
                {
                    _registrationRetries.erase(retry);
                    continue;
                }

                if (RegisterReadyBot(player, now, false))
                    _registrationRetries.erase(retry);
                else
                    RetryRegistration(entry.guid, retry->second, now);

                ++processed;
            }
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
            {
                QueueRegistration(guid, now);
                return;
            }

            RegisterReadyBot(player, now, true);
            auto const inserted = _states.find(guid);
            if (inserted == _states.end())
                return;
            LazyBotState& state = inserted->second;
            state.consecutiveDiscoveryMisses = 0;
            Schedule(guid, state, state.intent.IsActive() ? ScheduleLane::Active : ScheduleLane::Discovery, now);
        }

        void RemoveBot(uint64 guid)
        {
            _registrationRetries.erase(guid);
            _controlBots.erase(guid);
            auto const stateItr = _states.find(guid);
            if (stateItr == _states.end())
                return;

            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(guid));
            PlayerbotAI* botAI = player ? GET_PLAYERBOT_AI(player) : nullptr;
            if (player && botAI)
            {
                RecordIntentEnded(stateItr->second, IntentEndReason::Cancelled, getMSTime());
                ReleaseIntent(player, botAI, stateItr->second, "bot logged out");
            }

            _states.erase(stateItr);
        }

        void DrainPendingEvents(SchedulerTime now)
        {
            std::vector<PendingBotEvent> events;
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
                    QueueRegistration(event.guid, now);
                else
                    WakeBot(player, now);
            }
        }

        void ReconcileStrictRoster(SchedulerTime now)
        {
            if (_rosterReconcileRequested ||
                (_rosterCursor >= _rosterSnapshot.size() && now >= _nextRosterReconcileAt))
            {
                _rosterSnapshot.assign(sStrictAltbotMgr->GetRoster().begin(), sStrictAltbotMgr->GetRoster().end());
                _rosterCursor = 0;
                _rosterReconcileRequested = false;
                _nextRosterReconcileAt = now + std::chrono::milliseconds(ROSTER_RECONCILE_INTERVAL_MS);
            }

            uint32 processed = 0;
            while (_rosterCursor < _rosterSnapshot.size() && processed < MAX_ROSTER_RECONCILE_PER_TICK)
            {
                ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(_rosterSnapshot[_rosterCursor++]);
                if (ObjectAccessor::FindPlayer(guid))
                    QueueRegistration(guid.GetRawValue(), now);
                ++processed;
            }
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
                   player->IsBeingTeleported() || player->IsInFlight();
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

            uint32 const nowMs = getMSTime();
            TravelTarget* current = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

            if (!IsIntentQuestStateValid(player, state.intent))
            {
                RecordIntentEnded(state, DidIntentSucceed(player, state.intent)
                    ? IntentEndReason::Success
                    : IntentEndReason::Cancelled, nowMs);
                ReleaseIntent(player, botAI, state, "quest state changed");
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(POST_INTERACTION_RETRY_MS));
                return;
            }

            bool const semanticProgress = ObserveQuestProgress(player, state.intent);
            if (semanticProgress)
            {
                ++_metrics.semanticProgress;
                ++MetricsFor(state).semanticProgress;
            }

            bool const externallyPreempted = current &&
                (current->isGroupCopy() || (current->isForced() && !IsTravelTargetForIntent(current, state.intent)));
            if (IsTemporarilyUnavailable(player) || externallyPreempted)
            {
                if (!state.intent.suspended)
                {
                    ++_metrics.preemptions;
                    ++MetricsFor(state).preemptions;
                }
                PauseIntentTracking(player, state.intent, nowMs);
                Schedule(guid, state, ScheduleLane::Active,
                         now + std::chrono::milliseconds(TRANSIENT_RETRY_MS));
                return;
            }

            if (HasProtectedActivity(botAI))
            {
                ++_metrics.preemptions;
                ++MetricsFor(state).preemptions;
                char const* reason = HasAvailableLoot(botAI)
                    ? "loot available"
                    : "protected RPG activity";
                RecordIntentEnded(state, IntentEndReason::ProtectedActivity, nowMs);
                ReleaseIntent(player, botAI, state, reason);
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(_config.discoveryIntervalMs));
                return;
            }

            if (state.experimentMode == LazyQuestExperimentMode::AssistOnly &&
                getMSTimeDiff(state.intent.startedAtMs, nowMs) >= CONSERVATIVE_INTENT_LEASE_MS)
            {
                AddQuestCooldown(state, state.intent.questId, now, CONSERVATIVE_QUEST_COOLDOWN_MS);
                RecordIntentEnded(state, IntentEndReason::ConservativeLease, nowMs);
                ReleaseIntent(player, botAI, state, "conservative intent lease expired");
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(_config.discoveryIntervalMs));
                return;
            }

            bool currentActive = false;
            if (current && IsTravelTargetForIntent(current, state.intent))
            {
                // Relation destinations in Playerbots still depend on the retired "rpg quest" strategy.
                // Interact before isActive() can put a successfully reached relation target into cooldown.
                if (TryQuestInteraction(player, botAI, state.intent, current, nowMs))
                {
                    ++_metrics.semanticProgress;
                    ++MetricsFor(state).semanticProgress;
                    Schedule(guid, state, ScheduleLane::Active,
                             now + std::chrono::milliseconds(POST_INTERACTION_RETRY_MS));
                    return;
                }

                if (current->getStatus() == TRAVEL_STATUS_PREPARE)
                {
                    PauseIntentTracking(player, state.intent, nowMs);
                    Schedule(guid, state, ScheduleLane::Active,
                             now + std::chrono::milliseconds(TRANSIENT_RETRY_MS));
                    return;
                }

                currentActive = current->isActive();
                TravelStatus const status = current->getStatus();
                if (currentActive && status != TRAVEL_STATUS_COOLDOWN && status != TRAVEL_STATUS_EXPIRED)
                {
                    if (current->getDestination() != state.intent.destination ||
                        current->getPosition() != state.intent.point)
                        AdoptIntentLeg(player, state.intent, current, nowMs);

                    if (state.intent.type == LazyQuestIntentType::DoQuest && status == TRAVEL_STATUS_WORK)
                        current->setForced(false);

                    UpdateIntentProgress(player, state.intent, current, nowMs, semanticProgress);

                    if (state.experimentMode == LazyQuestExperimentMode::AssistOnly &&
                        (state.intent.noMovementMs >= CONSERVATIVE_NO_PROGRESS_MS ||
                         state.intent.noApproachMs >= CONSERVATIVE_NO_PROGRESS_MS))
                    {
                        AddQuestCooldown(state, state.intent.questId, now, CONSERVATIVE_QUEST_COOLDOWN_MS);
                        RecordIntentEnded(state, IntentEndReason::ConservativeLease, nowMs);
                        ReleaseIntent(player, botAI, state, "conservative intent stopped approaching");
                        Schedule(guid, state, ScheduleLane::Discovery,
                                 now + std::chrono::milliseconds(_config.discoveryIntervalMs));
                        return;
                    }
                }
            }

            if (!currentActive || IntentNeedsRecovery(state.intent, current))
            {
                uint32 const failedQuestId = state.intent.questId;
                IntentRecoveryResult const recovery =
                    RecoverIntentLeg(player, state.intent, current, nowMs);
                if (recovery == IntentRecoveryResult::Repointed)
                {
                    ++_metrics.repoints;
                    ++MetricsFor(state).repoints;
                    Schedule(guid, state, ScheduleLane::Active,
                             now + std::chrono::milliseconds(_config.activeCheckIntervalMs));
                    return;
                }

                bool const hardFailure = recovery == IntentRecoveryResult::HardFailed;
                AddQuestCooldown(state, failedQuestId, now,
                                 hardFailure ? FAILED_QUEST_COOLDOWN_MS : EXHAUSTED_QUEST_COOLDOWN_MS);
                if (hardFailure)
                    ++_metrics.hardStalls;
                else
                    ++_metrics.exhaustedIntents;

                RecordIntentEnded(state,
                    hardFailure ? IntentEndReason::HardStall : IntentEndReason::Exhausted, nowMs);

                LOG_DEBUG("playerbots", "[LQ] {} deferred {} quest {} after {} failed leg(s)",
                          player->GetName(), IntentTypeName(state.intent.type), failedQuestId,
                          state.intent.legFailures);
                ReleaseIntent(player, botAI, state, hardFailure ? "hard quest stall" : "no alternate quest leg");
                Schedule(guid, state, ScheduleLane::Discovery,
                         now + std::chrono::milliseconds(_config.discoveryIntervalMs));
                return;
            }

            Schedule(guid, state, ScheduleLane::Active,
                     now + std::chrono::milliseconds(_config.activeCheckIntervalMs));
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
            bool const allowQuestWork = state.experimentMode == LazyQuestExperimentMode::Current;
            bool const conservativeAssist = state.experimentMode == LazyQuestExperimentMode::AssistOnly;
            bool const found = FindLazyQuestCandidate(player, candidate, &coolingDown, &selectionStats,
                                                      allowQuestWork,
                                                      conservativeAssist ? CONSERVATIVE_TURN_IN_DISTANCE : 2500.0f,
                                                      conservativeAssist ? CONSERVATIVE_PICKUP_DISTANCE : 0.0f);
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

            AcquireIntent(player, botAI, state, candidate, current, getMSTime());
            RecordIntentAcquired(state);
            AssignIntentLeg(player, state.intent, current, candidate, getMSTime());
            Schedule(guid, state, ScheduleLane::Active,
                     now + std::chrono::milliseconds(_config.activeCheckIntervalMs));
        }

        void LogMetricsIfDue(SchedulerTime now)
        {
            if (now < _nextMetricsAt)
                return;

            std::size_t activeIntents = 0;
            std::size_t assistOnlyBots = 0;
            std::size_t currentBots = 0;
            for (auto const& state : _states)
            {
                if (state.second.intent.IsActive())
                    ++activeIntents;
                if (state.second.experimentMode == LazyQuestExperimentMode::AssistOnly)
                    ++assistOnlyBots;
                else
                    ++currentBots;
            }

            double const averageDiscoveryMicros = _metrics.selectorRuns
                ? static_cast<double>(_metrics.discoveryMicros) / _metrics.selectorRuns
                : 0.0;

            LOG_INFO("playerbots",
                     "[LQ] scheduler run={}: roster/registered/pending={}/{}/{}, modes control/assist/current={}/{}/{}, "
                     "intents={}, queues={}/{}, "
                     "processed={}/{}, registrations={}/{}/{}, progress/repoints/preemptions={}/{}/{}, "
                     "exhausted/hard-stalls={}/{}, selector avg/max={:.0f}/{}us, "
                     "indexed/evaluated/candidates={}/{}/{}, budget-limited ticks={}",
                     _config.experimentRunId, sStrictAltbotMgr->GetRosterSize(), _states.size(),
                     _registrationRetries.size(),
                     _controlBots.size(), assistOnlyBots, currentBots,
                     activeIntents, _activeQueue.size(), _discoveryQueue.size(),
                     _metrics.activeProcessed, _metrics.discoveryProcessed,
                     _metrics.registrationsCompleted, _metrics.registrationRetries,
                     _metrics.registrationTimeouts, _metrics.semanticProgress, _metrics.repoints,
                     _metrics.preemptions, _metrics.exhaustedIntents, _metrics.hardStalls,
                     averageDiscoveryMicros, _metrics.discoveryMaxMicros, _metrics.indexedPointsVisited,
                     _metrics.pickupDestinationsEvaluated, _metrics.candidatesFound,
                     _metrics.budgetLimitedTicks);

            std::array<LazyQuestExperimentMode, EXPERIMENT_MODE_COUNT> const modes = {
                LazyQuestExperimentMode::Control,
                LazyQuestExperimentMode::AssistOnly,
                LazyQuestExperimentMode::Current,
            };
            std::array<LazyQuestIntentType, INTENT_TYPE_COUNT> const types = {
                LazyQuestIntentType::PickUp,
                LazyQuestIntentType::DoQuest,
                LazyQuestIntentType::TurnIn,
            };

            for (LazyQuestExperimentMode mode : modes)
            {
                for (LazyQuestIntentType type : types)
                {
                    IntentMetrics const& metrics =
                        _intentMetrics[ExperimentModeIndex(mode)][IntentTypeIndex(type)];
                    if (!metrics.HasData())
                        continue;

                    double const averageDistance = metrics.acquired
                        ? static_cast<double>(metrics.distanceYards) / metrics.acquired
                        : 0.0;
                    double const averageDurationSeconds = metrics.Ended()
                        ? static_cast<double>(metrics.durationMs) / metrics.Ended() / IN_MILLISECONDS
                        : 0.0;

                    LOG_INFO("playerbots",
                             "[LQ][intent] run={} mode={} type={} acquired/progress/repoints/preemptions="
                             "{}/{}/{}/{}, endings success/protected/lease/exhausted/hard/cancelled="
                             "{}/{}/{}/{}/{}/{}, distance avg/max={:.1f}/{}, duration-avg={:.1f}s, "
                             "source none/null/grind/explore/rpg/quest/other={}/{}/{}/{}/{}/{}/{}",
                             _config.experimentRunId, ExperimentModeName(mode), IntentTypeName(type),
                             metrics.acquired, metrics.semanticProgress, metrics.repoints, metrics.preemptions,
                             metrics.endings[static_cast<std::size_t>(IntentEndReason::Success)],
                             metrics.endings[static_cast<std::size_t>(IntentEndReason::ProtectedActivity)],
                             metrics.endings[static_cast<std::size_t>(IntentEndReason::ConservativeLease)],
                             metrics.endings[static_cast<std::size_t>(IntentEndReason::Exhausted)],
                             metrics.endings[static_cast<std::size_t>(IntentEndReason::HardStall)],
                             metrics.endings[static_cast<std::size_t>(IntentEndReason::Cancelled)],
                             averageDistance, metrics.maxDistanceYards, averageDurationSeconds,
                             metrics.previousTargets[static_cast<std::size_t>(PreviousTargetType::None)],
                             metrics.previousTargets[static_cast<std::size_t>(PreviousTargetType::Null)],
                             metrics.previousTargets[static_cast<std::size_t>(PreviousTargetType::Grind)],
                             metrics.previousTargets[static_cast<std::size_t>(PreviousTargetType::Explore)],
                             metrics.previousTargets[static_cast<std::size_t>(PreviousTargetType::Rpg)],
                             metrics.previousTargets[static_cast<std::size_t>(PreviousTargetType::Quest)],
                             metrics.previousTargets[static_cast<std::size_t>(PreviousTargetType::Other)]);
                }
            }

            _metrics = {};
            _intentMetrics = {};
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
            _controlBots.clear();
            _activeQueue = {};
            _discoveryQueue = {};
            _registrationRetries.clear();
            _registrationQueue = {};
            _rosterSnapshot.clear();
            _rosterCursor = 0;
            _nextRosterReconcileAt = SchedulerTime::min();
            _metrics = {};
            _intentMetrics = {};
            _clearRequested = false;
        }

        LazyQuestingConfig _config;
        std::unordered_map<uint64, LazyBotState> _states;
        std::unordered_set<uint64> _controlBots;
        ScheduleQueue _activeQueue;
        ScheduleQueue _discoveryQueue;
        std::unordered_map<uint64, RegistrationRetryState> _registrationRetries;
        RegistrationQueue _registrationQueue;
        std::vector<uint32> _rosterSnapshot;
        std::size_t _rosterCursor = 0;
        std::mutex _pendingEventsMutex;
        std::deque<PendingBotEvent> _pendingEvents;
        SchedulerMetrics _metrics;
        std::array<std::array<IntentMetrics, INTENT_TYPE_COUNT>, EXPERIMENT_MODE_COUNT> _intentMetrics{};
        SchedulerTime _nextIndexAttempt = SchedulerTime::min();
        SchedulerTime _nextMetricsAt = SchedulerTime::max();
        SchedulerTime _nextRosterReconcileAt = SchedulerTime::min();
        bool _clearRequested = false;
        bool _rosterReconcileRequested = false;
    };

    class LazyQuestFlightRecorder
    {
    public:
        static LazyQuestFlightRecorder& Instance()
        {
            static LazyQuestFlightRecorder recorder;
            return recorder;
        }

        void Configure(LazyQuestingConfig const& config)
        {
            bool const wasEnabled = _config.flightRecorderEnabled;
            _config = config;

            if (wasEnabled && !_config.flightRecorderEnabled)
                _clearRequested = true;
            else if (!wasEnabled && _config.flightRecorderEnabled)
                Start();
        }

        void Start()
        {
            SchedulerTime const now = SchedulerClock::now();
            _nextSampleAt = now;
            _nextMetricsAt = now + std::chrono::milliseconds(_config.flightRecorderMetricsIntervalMs);
            _nextReconcileAt = now;
        }

        void RecordLogin(Player* player)
        {
            MutatePending(player, [](FlightRecorderPending& pending)
            {
                pending.registered = true;
                pending.registeredAt = SchedulerClock::now();
                pending.removed = false;
            });
        }

        void RecordLogout(Player* player)
        {
            MutatePending(player, [](FlightRecorderPending& pending)
            {
                pending.removed = true;
                pending.removedAt = SchedulerClock::now();
            });
        }

        void RecordXp(Player* player, uint32 amount, uint8 source)
        {
            MutatePending(player, [amount, source](FlightRecorderPending& pending)
            {
                switch (source)
                {
                    case XPSOURCE_KILL:
                        pending.counters.xpKill += amount;
                        break;
                    case XPSOURCE_QUEST:
                    case XPSOURCE_QUEST_DF:
                        pending.counters.xpQuest += amount;
                        break;
                    case XPSOURCE_EXPLORE:
                        pending.counters.xpExplore += amount;
                        break;
                    default:
                        pending.counters.xpOther += amount;
                        break;
                }
            });
        }

        void RecordKill(Player* player)
        {
            MutatePending(player, [](FlightRecorderPending& pending) { ++pending.counters.kills; });
        }

        void RecordDeath(Player* player)
        {
            MutatePending(player, [](FlightRecorderPending& pending) { ++pending.counters.deaths; });
        }

        void RecordLoot(Player* player)
        {
            MutatePending(player, [](FlightRecorderPending& pending) { ++pending.counters.lootEvents; });
        }

        void RecordLootItem(Player* player, Item* item)
        {
            ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
            uint32 const itemLevel = itemTemplate ? itemTemplate->ItemLevel : 0;
            bool const isGear = itemTemplate && itemTemplate->InventoryType != INVTYPE_NON_EQUIP;
            MutatePending(player, [itemLevel, isGear](FlightRecorderPending& pending)
            {
                ++pending.counters.lootItems;
                if (isGear)
                {
                    ++pending.counters.lootGearItems;
                    pending.counters.lootGearItemLevels += itemLevel;
                }
            });
        }

        void RecordQuestRewardItem(Player* player, Item* item)
        {
            ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
            if (!itemTemplate || itemTemplate->InventoryType == INVTYPE_NON_EQUIP)
                return;

            uint32 const itemLevel = itemTemplate->ItemLevel;
            MutatePending(player, [itemLevel](FlightRecorderPending& pending)
            {
                ++pending.counters.questRewardGearItems;
                pending.counters.questRewardGearItemLevels += itemLevel;
            });
        }

        void RecordEquip(Player* player, Item* item)
        {
            ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
            if (!itemTemplate || itemTemplate->InventoryType == INVTYPE_NON_EQUIP)
                return;

            uint32 const itemLevel = itemTemplate->ItemLevel;
            MutatePending(player, [itemLevel](FlightRecorderPending& pending)
            {
                ++pending.counters.equipEvents;
                pending.counters.equippedEventItemLevels += itemLevel;
            });
        }

        void RecordQuestPickup(Player* player)
        {
            MutatePending(player, [](FlightRecorderPending& pending) { ++pending.counters.questPickups; });
        }

        void RecordQuestTurnIn(Player* player, uint32 questId)
        {
            MutatePending(player, [questId](FlightRecorderPending& pending)
            {
                ++pending.counters.questTurnIns;
                if (questId)
                    ++pending.completedQuestCounts[questId];
            });
        }

        void RecordLevelUp(Player* player)
        {
            MutatePending(player, [](FlightRecorderPending& pending) { ++pending.counters.levelUps; });
        }

        void Update()
        {
            SchedulerTime const now = SchedulerClock::now();
            DrainPending(now);

            if (_clearRequested)
            {
                SampleBots(now);
                LogMetrics(now, true);
                Clear();
                _clearRequested = false;
            }

            if (!_config.flightRecorderEnabled)
                return;

            if (now >= _nextReconcileAt)
            {
                Reconcile(now);
                _nextReconcileAt = now + std::chrono::milliseconds(ROSTER_RECONCILE_INTERVAL_MS);
            }

            if (now >= _nextSampleAt)
            {
                SampleBots(now);
                _nextSampleAt = now + std::chrono::milliseconds(_config.flightRecorderSampleIntervalMs);
            }

            LogMetrics(now, false);
        }

        void Shutdown()
        {
            SchedulerTime const now = SchedulerClock::now();
            DrainPending(now);
            SampleBots(now);
            LogMetrics(now, true);
            Clear();
        }

    private:
        LazyQuestFlightRecorder() = default;

        template <typename Callback>
        void MutatePending(Player* player, Callback callback)
        {
            if (!_config.flightRecorderEnabled || !IsLazyQuestingBot(player))
                return;

            uint64 const guid = player->GetGUID().GetRawValue();
            LazyQuestExperimentMode const mode = GetExperimentMode(player, _config);
            std::lock_guard<std::mutex> lock(_pendingMutex);
            FlightRecorderPending& pending = _pending[guid];
            pending.mode = mode;
            callback(pending);
        }

        static std::size_t ActivityIndex(RecordedActivity activity)
        {
            return static_cast<std::size_t>(activity);
        }

        void ObserveEquipment(Player* player, FlightRecorderBotState& state) const
        {
            uint32 totalItemLevel = 0;
            uint32 occupiedSlots = 0;
            uint32 weaponItemLevel = 0;

            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
                if (!itemTemplate)
                    continue;

                ++occupiedSlots;
                totalItemLevel += itemTemplate->ItemLevel;
                if (slot == EQUIPMENT_SLOT_MAINHAND || slot == EQUIPMENT_SLOT_RANGED)
                    weaponItemLevel = std::max(weaponItemLevel, itemTemplate->ItemLevel);
            }

            uint32 const averageItemLevel = occupiedSlots
                ? (totalItemLevel + occupiedSlots / 2) / occupiedSlots
                : 0;

            ++state.counters.gearSamples;
            state.counters.sampledLevels += player->GetLevel();
            state.counters.sampledAverageItemLevels += averageItemLevel;
            state.counters.sampledTotalItemLevels += totalItemLevel;
            state.counters.sampledOccupiedSlots += occupiedSlots;
            state.counters.sampledWeaponItemLevels += weaponItemLevel;
        }

        uint32 GetQuestProgressValue(QuestStatusData const& status) const
        {
            uint32 progress = status.PlayerCount + (status.Explored ? 1 : 0);
            for (uint16 count : status.ItemCount)
                progress += count;
            for (uint16 count : status.CreatureOrGOCount)
                progress += count;
            return progress;
        }

        RecordedActivity ClassifyActivity(Player* player) const
        {
            if (!player->IsAlive())
                return RecordedActivity::Dead;
            if (player->IsInCombat())
                return RecordedActivity::Fighting;

            PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
            TravelTarget* travelTarget = nullptr;
            TravelDestination* destination = nullptr;
            NewRpgStatus rpgStatus = RPG_IDLE;

            if (botAI)
            {
                AiObjectContext* context = botAI->GetAiObjectContext();
                if (context->GetValue<bool>("has available loot")->Get())
                    return RecordedActivity::Looting;

                travelTarget = context->GetValue<TravelTarget*>("travel target")->Get();
                destination = travelTarget ? travelTarget->getDestination() : nullptr;
                rpgStatus = botAI->rpgInfo.GetStatus();

                if (HasEssentialRpgNeed(botAI) &&
                    (dynamic_cast<RpgTravelDestination*>(destination) || rpgStatus == RPG_WANDER_NPC))
                    return RecordedActivity::Servicing;

                if ((travelTarget && travelTarget->getStatus() == TRAVEL_STATUS_WORK &&
                     dynamic_cast<QuestRelationTravelDestination*>(destination)) ||
                    rpgStatus == RPG_WANDER_NPC)
                    return RecordedActivity::Interacting;
            }

            if (player->IsInFlight() || player->IsBeingTeleported() || player->HasUnitState(UNIT_STATE_MOVING) ||
                (travelTarget && (travelTarget->getStatus() == TRAVEL_STATUS_PREPARE ||
                                  travelTarget->getStatus() == TRAVEL_STATUS_TRAVEL)))
                return RecordedActivity::Travelling;

            return RecordedActivity::Idle;
        }

        void InitializeSnapshots(Player* player, FlightRecorderBotState& state, SchedulerTime now)
        {
            state.questProgress.clear();
            state.completedQuests.clear();
            for (auto const& quest : player->getQuestStatusMap())
            {
                if (quest.second.Status == QUEST_STATUS_INCOMPLETE || quest.second.Status == QUEST_STATUS_COMPLETE)
                {
                    state.questProgress[quest.first] = GetQuestProgressValue(quest.second);
                    if (quest.second.Status == QUEST_STATUS_COMPLETE)
                        state.completedQuests.insert(quest.first);
                }
            }

            state.lastActivity = ClassifyActivity(player);
            state.lastObservedAt = now;
            state.observed = true;
            state.retired = false;
            UpdateIntentSnapshot(player, state);
        }

        void UpdateIntentSnapshot(Player* player, FlightRecorderBotState& state)
        {
            uint32 questId = 0;
            LazyQuestIntentType type = LazyQuestIntentType::DoQuest;
            if (LazyQuestingScheduler::Instance().GetIntentSnapshot(player->GetGUID().GetRawValue(), questId, type))
            {
                state.currentIntentQuestId = questId;
                state.currentIntentType = type;
            }
            else
            {
                state.currentIntentQuestId = 0;
                state.currentIntentType = LazyQuestIntentType::DoQuest;
            }
        }

        void AccrueActivity(FlightRecorderBotState& state, SchedulerTime until)
        {
            if (!state.observed || until <= state.lastObservedAt)
                return;

            uint64 const elapsedMs = static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                until - state.lastObservedAt).count());
            state.counters.activityMs[ActivityIndex(state.lastActivity)] += elapsedMs;
            state.lastObservedAt = until;
        }

        void ObserveQuestProgress(Player* player, FlightRecorderBotState& state)
        {
            std::unordered_set<uint32> activeQuests;
            activeQuests.reserve(player->getQuestStatusMap().size());

            for (auto const& quest : player->getQuestStatusMap())
            {
                if (quest.second.Status != QUEST_STATUS_INCOMPLETE && quest.second.Status != QUEST_STATUS_COMPLETE)
                    continue;

                activeQuests.insert(quest.first);
                uint32 const progress = GetQuestProgressValue(quest.second);
                auto const previous = state.questProgress.find(quest.first);
                if (previous == state.questProgress.end())
                {
                    state.questProgress.emplace(quest.first, progress);
                    state.counters.objectiveDeltas += progress;
                }
                else
                {
                    if (progress > previous->second)
                        state.counters.objectiveDeltas += progress - previous->second;
                    previous->second = progress;
                }

                if (quest.second.Status == QUEST_STATUS_COMPLETE)
                {
                    if (state.completedQuests.insert(quest.first).second)
                        ++state.counters.questCompletions;
                }
                else
                    state.completedQuests.erase(quest.first);
            }

            for (auto itr = state.questProgress.begin(); itr != state.questProgress.end();)
            {
                if (!activeQuests.count(itr->first))
                {
                    state.completedQuests.erase(itr->first);
                    itr = state.questProgress.erase(itr);
                }
                else
                    ++itr;
            }
        }

        void CarryCounters(uint64 /*guid*/, FlightRecorderBotState& state)
        {
            if (!state.counters.HasData())
                return;

            std::size_t const modeIndex = ExperimentModeIndex(state.mode);
            _carried[modeIndex].Add(state.counters);
            ++_carriedBots[modeIndex];
            state.counters = {};
        }

        FlightRecorderBotState& EnsureState(uint64 guid, Player* player, LazyQuestExperimentMode mode,
                                            SchedulerTime now)
        {
            auto const inserted = _states.try_emplace(guid);
            FlightRecorderBotState& state = inserted.first->second;

            if (!inserted.second && state.mode != mode)
                CarryCounters(guid, state);
            state.mode = mode;

            if (player && (!state.observed || state.retired))
                InitializeSnapshots(player, state, now);
            return state;
        }

        void DrainPending(SchedulerTime now)
        {
            std::unordered_map<uint64, FlightRecorderPending> pending;
            {
                std::lock_guard<std::mutex> lock(_pendingMutex);
                pending.swap(_pending);
            }

            for (auto& event : pending)
            {
                Player* player = ObjectAccessor::FindPlayer(ObjectGuid(event.first));
                if (player && !IsLazyQuestingBot(player))
                    player = nullptr;

                FlightRecorderBotState& state = EnsureState(event.first, player, event.second.mode, now);
                state.counters.Add(event.second.counters);

                if (event.second.registered && player)
                    InitializeSnapshots(player, state, event.second.registeredAt);

                for (auto const& completion : event.second.completedQuestCounts)
                {
                    uint32 const alreadyObserved = state.completedQuests.count(completion.first) ? 1 : 0;
                    state.counters.questCompletions += completion.second - alreadyObserved;
                    state.completedQuests.insert(completion.first);
                }

                if (event.second.removed)
                {
                    AccrueActivity(state, event.second.removedAt);
                    state.retired = true;
                    state.currentIntentQuestId = 0;
                }
            }
        }

        void Reconcile(SchedulerTime now)
        {
            for (uint32 lowGuid : sStrictAltbotMgr->GetRoster())
            {
                ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
                Player* player = ObjectAccessor::FindPlayer(guid);
                if (!player)
                    continue;

                EnsureState(guid.GetRawValue(), player, GetExperimentMode(player, _config), now);
            }
        }

        void SampleBots(SchedulerTime now)
        {
            for (auto& entry : _states)
            {
                FlightRecorderBotState& state = entry.second;
                if (state.retired)
                    continue;

                Player* player = ObjectAccessor::FindPlayer(ObjectGuid(entry.first));
                if (!player || !IsLazyQuestingBot(player) || !player->IsInWorld())
                {
                    AccrueActivity(state, now);
                    state.retired = true;
                    state.currentIntentQuestId = 0;
                    continue;
                }

                LazyQuestExperimentMode const mode = GetExperimentMode(player, _config);
                if (state.mode != mode)
                {
                    CarryCounters(entry.first, state);
                    state.mode = mode;
                }

                AccrueActivity(state, now);
                ObserveQuestProgress(player, state);
                UpdateIntentSnapshot(player, state);
                ObserveEquipment(player, state);
                state.lastActivity = ClassifyActivity(player);
            }
        }

        void LogMetrics(SchedulerTime now, bool force)
        {
            if (!force && now < _nextMetricsAt)
                return;

            std::array<FlightRecorderCounters, EXPERIMENT_MODE_COUNT> totals = _carried;
            std::array<uint64, EXPERIMENT_MODE_COUNT> botCounts = _carriedBots;
            std::array<uint64, EXPERIMENT_MODE_COUNT> intentCounts{};

            for (auto const& entry : _states)
            {
                std::size_t const modeIndex = ExperimentModeIndex(entry.second.mode);
                totals[modeIndex].Add(entry.second.counters);
                ++botCounts[modeIndex];
                if (entry.second.currentIntentQuestId)
                    ++intentCounts[modeIndex];
            }

            std::array<LazyQuestExperimentMode, EXPERIMENT_MODE_COUNT> const modes = {
                LazyQuestExperimentMode::Control,
                LazyQuestExperimentMode::AssistOnly,
                LazyQuestExperimentMode::Current,
            };

            for (LazyQuestExperimentMode mode : modes)
            {
                std::size_t const modeIndex = ExperimentModeIndex(mode);
                FlightRecorderCounters const& counters = totals[modeIndex];
                if (!botCounts[modeIndex] && !counters.HasData())
                    continue;

                LOG_INFO("playerbots",
                         "[LQ][flight] run={} mode={} bots={} xp total/kill/quest/explore/other={}/{}/{}/{}/{}, "
                         "activity-s travel/fight/loot/interact/service/dead/idle={}/{}/{}/{}/{}/{}/{}, "
                         "events kills/deaths/loot/pickups/objective-deltas/completions/turn-ins/levels="
                         "{}/{}/{}/{}/{}/{}/{}/{}, intents={}",
                         _config.experimentRunId, ExperimentModeName(mode), botCounts[modeIndex],
                         counters.TotalXp(), counters.xpKill,
                         counters.xpQuest, counters.xpExplore, counters.xpOther,
                         counters.activityMs[ActivityIndex(RecordedActivity::Travelling)] / IN_MILLISECONDS,
                         counters.activityMs[ActivityIndex(RecordedActivity::Fighting)] / IN_MILLISECONDS,
                         counters.activityMs[ActivityIndex(RecordedActivity::Looting)] / IN_MILLISECONDS,
                         counters.activityMs[ActivityIndex(RecordedActivity::Interacting)] / IN_MILLISECONDS,
                         counters.activityMs[ActivityIndex(RecordedActivity::Servicing)] / IN_MILLISECONDS,
                         counters.activityMs[ActivityIndex(RecordedActivity::Dead)] / IN_MILLISECONDS,
                         counters.activityMs[ActivityIndex(RecordedActivity::Idle)] / IN_MILLISECONDS,
                         counters.kills, counters.deaths, counters.lootEvents, counters.questPickups,
                         counters.objectiveDeltas, counters.questCompletions, counters.questTurnIns,
                         counters.levelUps, intentCounts[modeIndex]);

                double const averageLevel = counters.gearSamples
                    ? static_cast<double>(counters.sampledLevels) / counters.gearSamples
                    : 0.0;
                double const averageItemLevel = counters.gearSamples
                    ? static_cast<double>(counters.sampledAverageItemLevels) / counters.gearSamples
                    : 0.0;
                double const averageTotalItemLevel = counters.gearSamples
                    ? static_cast<double>(counters.sampledTotalItemLevels) / counters.gearSamples
                    : 0.0;
                double const averageOccupiedSlots = counters.gearSamples
                    ? static_cast<double>(counters.sampledOccupiedSlots) / counters.gearSamples
                    : 0.0;
                double const averageWeaponItemLevel = counters.gearSamples
                    ? static_cast<double>(counters.sampledWeaponItemLevels) / counters.gearSamples
                    : 0.0;

                LOG_INFO("playerbots",
                         "[LQ][gear] run={} mode={} bots={} samples={} "
                         "snapshot level/avg-ilvl/total-ilvl/slots/weapon-ilvl="
                         "{:.2f}/{:.2f}/{:.2f}/{:.2f}/{:.2f}, "
                         "items loot/loot-gear/quest-gear/equips={}/{}/{}/{}, "
                         "item-level-sums loot-gear/quest-gear/equips={}/{}/{}",
                         _config.experimentRunId, ExperimentModeName(mode), botCounts[modeIndex],
                         counters.gearSamples, averageLevel, averageItemLevel, averageTotalItemLevel,
                         averageOccupiedSlots, averageWeaponItemLevel, counters.lootItems,
                         counters.lootGearItems, counters.questRewardGearItems, counters.equipEvents,
                         counters.lootGearItemLevels, counters.questRewardGearItemLevels,
                         counters.equippedEventItemLevels);
            }

            _carried = {};
            _carriedBots = {};
            for (auto itr = _states.begin(); itr != _states.end();)
            {
                if (itr->second.retired)
                {
                    itr = _states.erase(itr);
                    continue;
                }

                itr->second.counters = {};
                ++itr;
            }
            _nextMetricsAt = now + std::chrono::milliseconds(_config.flightRecorderMetricsIntervalMs);
        }

        void Clear()
        {
            _states.clear();
            _carried = {};
            _carriedBots = {};
            {
                std::lock_guard<std::mutex> lock(_pendingMutex);
                _pending.clear();
            }
        }

        LazyQuestingConfig _config;
        std::unordered_map<uint64, FlightRecorderBotState> _states;
        std::array<FlightRecorderCounters, EXPERIMENT_MODE_COUNT> _carried{};
        std::array<uint64, EXPERIMENT_MODE_COUNT> _carriedBots{};
        std::mutex _pendingMutex;
        std::unordered_map<uint64, FlightRecorderPending> _pending;
        SchedulerTime _nextSampleAt = SchedulerTime::max();
        SchedulerTime _nextMetricsAt = SchedulerTime::max();
        SchedulerTime _nextReconcileAt = SchedulerTime::max();
        bool _clearRequested = false;
    };
}

class LazyQuestingPlayerScript final : public PlayerScript
{
public:
    LazyQuestingPlayerScript()
        : PlayerScript("LazyQuestingPlayerScript",
                       { PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
                         PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
                         PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_GIVE_EXP, PLAYERHOOK_ON_LOGIN,
                         PLAYERHOOK_ON_BEFORE_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED,
                         PLAYERHOOK_ON_PLAYER_QUEST_ACCEPT, PLAYERHOOK_ON_AFTER_CREATURE_LOOT,
                         PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_QUEST_REWARD_ITEM,
                         PLAYERHOOK_ON_EQUIP })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        LazyQuestFlightRecorder::Instance().RecordLogin(player);
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Register, player);
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        LazyQuestFlightRecorder::Instance().RecordLogout(player);
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Remove, player);
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        LazyQuestFlightRecorder::Instance().RecordQuestTurnIn(player, quest ? quest->GetQuestId() : 0);
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Wake, player);
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (player->GetLevel() > oldLevel)
            LazyQuestFlightRecorder::Instance().RecordLevelUp(player);
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Wake, player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        LazyQuestingScheduler::Instance().QueueEvent(PendingBotEventType::Wake, player);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 xpSource) override
    {
        LazyQuestFlightRecorder::Instance().RecordXp(player, amount, xpSource);
    }

    void OnPlayerCreatureKill(Player* player, Creature* /*killed*/) override
    {
        LazyQuestFlightRecorder::Instance().RecordKill(player);
    }

    void OnPlayerCreatureKilledByPet(Player* player, Creature* /*killed*/) override
    {
        LazyQuestFlightRecorder::Instance().RecordKill(player);
    }

    void OnPlayerJustDied(Player* player) override
    {
        LazyQuestFlightRecorder::Instance().RecordDeath(player);
    }

    void OnPlayerQuestAccept(Player* player, Quest const* /*quest*/) override
    {
        LazyQuestFlightRecorder::Instance().RecordQuestPickup(player);
    }

    void OnPlayerAfterCreatureLoot(Player* player) override
    {
        LazyQuestFlightRecorder::Instance().RecordLoot(player);
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootGuid*/) override
    {
        LazyQuestFlightRecorder::Instance().RecordLootItem(player, item);
    }

    void OnPlayerQuestRewardItem(Player* player, Item* item, uint32 /*count*/) override
    {
        LazyQuestFlightRecorder::Instance().RecordQuestRewardItem(player, item);
    }

    void OnPlayerEquip(Player* player, Item* item, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/) override
    {
        LazyQuestFlightRecorder::Instance().RecordEquip(player, item);
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
        LazyQuestFlightRecorder::Instance().Configure(config);
        LOG_INFO("server.loading",
                  "mod-lazy-questing config: enabled={}, budget={}ms, active={}ms, discovery={}..{}ms, "
                  "per-tick active/discovery={}/{}, experiment run={} seed={}, "
                  "control/assist/current={}/{}/{}%, "
                  "flight-recorder={} sample/metrics={}/{}ms.",
                  config.enabled, config.worldBudgetMs, config.activeCheckIntervalMs,
                  config.discoveryIntervalMs, config.maxDiscoveryBackoffMs,
                  config.maxActiveBotsPerTick, config.maxDiscoveryBotsPerTick, config.experimentRunId,
                  config.experimentSeed,
                  config.experimentControlPercent, config.experimentAssistOnlyPercent,
                  100 - config.experimentControlPercent - config.experimentAssistOnlyPercent,
                  config.flightRecorderEnabled, config.flightRecorderSampleIntervalMs,
                  config.flightRecorderMetricsIntervalMs);
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
        LazyQuestFlightRecorder::Instance().Start();
        LOG_INFO("server.loading", "mod-lazy-questing loaded.");
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        LazyQuestingScheduler::Instance().Update();
        LazyQuestFlightRecorder::Instance().Update();
    }

    void OnShutdown() override
    {
        LazyQuestFlightRecorder::Instance().Shutdown();
        LazyQuestingScheduler::Instance().Shutdown();
    }
};

void Addmod_lazy_questingScripts()
{
    new LazyQuestingWorldScript();
    new LazyQuestingPlayerScript();
}
