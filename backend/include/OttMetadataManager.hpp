#pragma once

#include "OttRestClient.hpp"
#include "OttMetadataResult.hpp"

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

struct OttMetadataConfig
{
    std::size_t maxAttempts = 3;

    std::chrono::milliseconds initialBackoff{250};

    std::chrono::milliseconds maxBackoff{2000};

    std::chrono::milliseconds servicesTtl{300000};

    std::chrono::milliseconds epgTtl{60000};

    std::chrono::milliseconds maxCacheAge{1800000};

    std::size_t maxEpgEntries = 64;
};

class OttMetadataManager
{
public:
    explicit OttMetadataManager(
        const OttRestConfig& restConfig =
            OttRestConfig{},

        const OttMetadataConfig& metadataConfig =
            OttMetadataConfig{}
    );

    OttMetadataManager(
        const OttMetadataManager&
    ) = delete;

    OttMetadataManager& operator=(
        const OttMetadataManager&
    ) = delete;

    OttMetadataResult<std::vector<TvService>>
    getServices(bool forceRefresh = false);

    OttMetadataResult<std::vector<EpgEvent>>
    getEpg(
        std::uint16_t serviceId,
        const std::string& date,
        bool forceRefresh = false
    );

    void clearCache();

    std::size_t epgCacheSize() const;

private:
    using Clock = std::chrono::steady_clock;

    using Services = std::vector<TvService>;

    using Events = std::vector<EpgEvent>;

    using EpgKey =
        std::pair<std::uint16_t, std::string>;

    template<typename T>
    struct CacheEntry
    {
        T data;

        Clock::time_point storedAt;
    };

    const OttRestClient client_;

    const OttMetadataConfig config_;

    mutable std::mutex mutex_;

    std::optional<CacheEntry<Services>>
        servicesCache_;

    std::map<EpgKey, CacheEntry<Events>>
        epgCache_;

    static bool retryable(
        OttRestStatus status,
        long httpStatus
    ) noexcept;

    static bool fallbackAllowed(
        OttRestStatus status,
        long httpStatus
    ) noexcept;

    template<typename T, typename Fetch>
    OttMetadataResult<T> fetchWithRetry(
        Fetch&& fetch
    ) const;

    template<typename T>
    static OttMetadataResult<T> cachedResult(
        const CacheEntry<T>& entry
    );

    void pruneExpiredEpgLocked(
        Clock::time_point now
    );

    void enforceEpgLimitLocked();
};
