#pragma once

#include "SessionController.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

struct ApiStateSnapshot
{
    SessionSnapshot session;

    std::optional<ActiveSessionStatistics> statistics;

    std::uint64_t revision = 0;

    bool initialized = false;

    std::chrono::steady_clock::time_point updatedAt{};
};

class ApiStateCache
{
public:
    ApiStateCache() = default;

    ApiStateCache(const ApiStateCache&) = delete;

    ApiStateCache& operator=(const ApiStateCache&) = delete;

    bool publish(
        const SessionSnapshot& session,
        const std::optional<ActiveSessionStatistics>& statistics
    );

    ApiStateSnapshot snapshot() const;

    bool initialized() const;

private:
    mutable std::mutex mutex_;

    ApiStateSnapshot state_;
};
