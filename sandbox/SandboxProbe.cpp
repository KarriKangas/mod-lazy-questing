#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SandboxClock.h"
#include "Timer.h"
#include <atomic>

namespace
{
    std::atomic<uint64> xp{0};
    std::atomic<uint64> kills{0};
    std::atomic<uint64> deaths{0};

    class SandboxPlayerProbe final : public PlayerScript
    {
    public:
        SandboxPlayerProbe() : PlayerScript("SandboxPlayerProbe",
            { PLAYERHOOK_ON_GIVE_EXP, PLAYERHOOK_ON_CREATURE_KILL,
              PLAYERHOOK_ON_CREATURE_KILLED_BY_PET, PLAYERHOOK_ON_PLAYER_JUST_DIED }) { }

        void OnPlayerGiveXP(Player*, uint32& amount, Unit*, uint8) override { xp.fetch_add(amount); }
        void OnPlayerCreatureKill(Player*, Creature*) override { kills.fetch_add(1); }
        void OnPlayerCreatureKilledByPet(Player*, Creature*) override { kills.fetch_add(1); }
        void OnPlayerJustDied(Player*) override { deaths.fetch_add(1); }
    };

    class SandboxWorldProbe final : public WorldScript
    {
    public:
        SandboxWorldProbe() : WorldScript("SandboxWorldProbe", { WORLDHOOK_ON_UPDATE }) { }

        void OnUpdate(uint32 diff) override
        {
            elapsed += diff;
            if (elapsed < 60000)
                return;
            elapsed %= 60000;
            uint32 online = 0;
            uint64 played = 0;
            for (auto const& entry : ObjectAccessor::GetPlayers())
            {
                Player* player = entry.second;
                if (player && player->IsInWorld())
                {
                    ++online;
                    played += player->GetTotalPlayedTime();
                }
            }
            LOG_INFO("server.worldserver",
                "[SANDBOX-PROBE] online={} total_played_s={} xp={} kills={} deaths={} game_s={} unix_s={} mono_ms={}",
                online, played, xp.load(), kills.load(), deaths.load(), GameTime::GetGameTime().count(),
                SandboxClock::UnixTime(), getMSTime());
        }

    private:
        uint32 elapsed = 0;
    };
}

void Addmod_lazy_questingScripts()
{
    new SandboxPlayerProbe();
    new SandboxWorldProbe();
}
