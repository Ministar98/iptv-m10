#include "ApiStateCache.hpp"

#include <limits>
#include <utility>

bool ApiStateCache::publish(
    const SessionSnapshot& session,
    const std::optional<ActiveSessionStatistics>& statistics
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_.initialized &&
        session.generationId < state_.session.generationId)
    {
        return false;
    }

    if (state_.revision ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }

    ApiStateSnapshot next;

    next.session = session;
    next.statistics = statistics;
    next.revision = state_.revision + 1;
    next.initialized = true;
    next.updatedAt = std::chrono::steady_clock::now();

    state_ = std::move(next);

    return true;
}

ApiStateSnapshot ApiStateCache::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return state_;
}

bool ApiStateCache::initialized() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return state_.initialized;
}
