#include "../SandboxClock.h"
#include <iostream>
#include <stdexcept>
#include <thread>

std::chrono::steady_clock::time_point PeerNow();

void Require(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main()
{
    using namespace std::chrono;
    if (char const* epoch = std::getenv("WOWBOT_LAB_EPOCH_NS"))
    {
        auto target = nanoseconds(std::strtoll(epoch, nullptr, 10));
        auto actual = duration_cast<nanoseconds>(SandboxClock::SystemNow().time_since_epoch());
        Require(actual >= target && actual - target < 1s, "persisted epoch not applied during startup");
    }
    SandboxClock::Start();
    auto steady = SandboxClock::SteadyNow();
    auto system = SandboxClock::SystemNow();
    std::this_thread::sleep_for(20ms);
    Require(SandboxClock::SteadyNow() == steady, "real sleep advanced paused simulation");
    Require(PeerNow() == steady, "translation units do not share the clock");
    for (int i = 0; i < 20; ++i)
        SandboxClock::Advance(50ms);
    Require(SandboxClock::SteadyNow() - steady == 1s, "monotonic clock drift");
    Require(SandboxClock::SystemNow() - system == 1s, "epoch clock drift");
    Require(PeerNow() == SandboxClock::SteadyNow(), "translation unit clock divergence");
    std::time_t epoch = 0;
    Require(SandboxClock::UnixTime(&epoch) == system_clock::to_time_t(system + 1s), "Unix clock mismatch");
    Require(epoch == SandboxClock::UnixTime(), "Unix output pointer mismatch");
    std::atomic<bool> failed{false};
    std::thread observer([&failed]
    {
        auto previous = SandboxClock::SteadyNow();
        for (int i = 0; i < 10000; ++i)
        {
            auto current = SandboxClock::SteadyNow();
            if (current < previous)
                failed.store(true);
            previous = current;
        }
    });
    for (int i = 0; i < 10000; ++i)
        SandboxClock::Advance(50ms);
    observer.join();
    Require(!failed.load(), "clock moved backwards across threads");
    Require(SandboxClock::SteadyNow() - steady == 501s, "fixed-step accumulation lost time");
    std::cout << "PASS: fixed steps, paused wall time, epoch agreement, shared state, monotonic concurrent reads\n";
}
