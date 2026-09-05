#ifndef WOWBOT_SANDBOX_CLOCK_H
#define WOWBOT_SANDBOX_CLOCK_H

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <stdexcept>

// Sandbox source overlay only. Never installed in the normal source checkout.
namespace SandboxClock
{
    struct State
    {
        State()
        {
            if (char const* epoch = std::getenv("WOWBOT_LAB_EPOCH_NS"))
            {
                char* end = nullptr;
                long long value = std::strtoll(epoch, &end, 10);
                if (value <= 0 || !end || *end)
                    throw std::runtime_error("Invalid sandbox epoch");
                system = std::chrono::system_clock::time_point(
                    std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(value)));
            }
        }

        std::chrono::steady_clock::time_point const steady = std::chrono::steady_clock::now();
        std::chrono::system_clock::time_point system = std::chrono::system_clock::now();
        std::atomic<long long> elapsedNs{0};
        std::atomic<bool> enabled{false};
    };

    inline State& GetState()
    {
        static State state;
        return state;
    }

    inline void Start()
    {
        auto& state = GetState();
        state.elapsedNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - state.steady).count());
        state.enabled.store(true);
    }

    inline void Advance(std::chrono::milliseconds step)
    {
        GetState().elapsedNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(step).count());
    }

    inline std::chrono::steady_clock::time_point SteadyNow()
    {
        auto& state = GetState();
        if (!state.enabled.load())
            return std::chrono::steady_clock::now();
        return state.steady + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::nanoseconds(state.elapsedNs.load()));
    }

    inline std::chrono::system_clock::time_point SystemNow()
    {
        auto& state = GetState();
        if (!state.enabled.load())
            return state.system + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::steady_clock::now() - state.steady);
        return state.system + std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(state.elapsedNs.load()));
    }

    inline std::time_t UnixTime(std::time_t* output = nullptr)
    {
        auto value = std::chrono::system_clock::to_time_t(SystemNow());
        if (output)
            *output = value;
        return value;
    }
}
#endif
