#include "../SandboxClock.h"

std::chrono::steady_clock::time_point PeerNow()
{
    return SandboxClock::SteadyNow();
}
