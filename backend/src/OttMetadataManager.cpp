#include "OttMetadataManager.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

OttMetadataManager::OttMetadataManager(
    const OttRestConfig& restConfig,
    const OttMetadataConfig& metadataConfig
)
    : client_(restConfig),
      config_(metadataConfig)
{
    if (
        config_.maxAttempts == 0 ||
        config_.maxAttempts > 10 ||
        config_.initialBackoff.count() <= 0 ||
        config_.maxBackoff <
            config_.initialBackoff ||
        config_.servicesTtl.count() < 0 ||
        config_.epgTtl.count() < 0 ||
        config_.maxCacheAge.count() <= 0 ||
        config_.maxCacheAge <
            config_.servicesTtl ||
        config_.maxCacheAge <
            config_.epgTtl ||
        config_.maxEpgEntries == 0
    )
    {
        throw std::invalid_argument(
            "Invalid OTT metadata configuration"
        );
    }
}

bool OttMetadataManager::retryable(
    OttRestStatus status,
    long httpStatus
) noexcept
{
    if (
        status == OttRestStatus::ConnectionError ||
        status == OttRestStatus::Timeout
    )
    {
        return true;
    }

    if (status == OttRestStatus::HttpError)
    {
        return httpStatus == 408 ||
               httpStatus == 429 ||
               (
                   httpStatus >= 500 &&
                   httpStatus <= 599
               );
    }

    return false;
}

bool OttMetadataManager::fallbackAllowed(
    OttRestStatus status,
    long httpStatus
) noexcept
{
    return retryable(status, httpStatus) ||
           status == OttRestStatus::InvalidResponse;
}

template<typename T, typename Fetch>
OttMetadataResult<T>
OttMetadataManager::fetchWithRetry(
    Fetch&& fetch
) const
{
    OttMetadataResult<T> result;

    auto backoff = config_.initialBackoff;

    for (
        std::size_t attempt = 1;
        attempt <= config_.maxAttempts;
        ++attempt
    )
    {
        auto response = fetch();

        result.attempts = attempt;

        result.networkStatus = response.status;

        result.httpStatus = response.httpStatus;

        result.errorMessage =
            std::move(response.errorMessage);

        if (response.success())
        {
            result.source =
                OttMetadataSource::Network;

            result.data =
                std::move(response.data);

            result.errorMessage.clear();

            return result;
        }

        if (
            attempt == config_.maxAttempts ||
            !retryable(
                response.status,
                response.httpStatus
            )
        )
        {
            break;
        }

        std::this_thread::sleep_for(backoff);

        if (backoff > config_.maxBackoff / 2)
        {
            backoff = config_.maxBackoff;
        }
        else
        {
            backoff = std::min(
                backoff * 2,
                config_.maxBackoff
            );
        }
    }

    return result;
}

template<typename T>
OttMetadataResult<T>
OttMetadataManager::cachedResult(
    const CacheEntry<T>& entry
)
{
    OttMetadataResult<T> result;

    result.source = OttMetadataSource::Cache;

    result.data = entry.data;

    return result;
}

OttMetadataResult<std::vector<TvService>>
OttMetadataManager::getServices(
    bool forceRefresh
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto now = Clock::now();

    if (
        servicesCache_ &&
        now - servicesCache_->storedAt >=
            config_.maxCacheAge
    )
    {
        servicesCache_.reset();
    }

    if (
        !forceRefresh &&
        servicesCache_ &&
        now - servicesCache_->storedAt <
            config_.servicesTtl
    )
    {
        return cachedResult(*servicesCache_);
    }

    auto result = fetchWithRetry<Services>(
        [this]()
        {
            return client_.fetchServices();
        }
    );

    if (result.success())
    {
        servicesCache_ = CacheEntry<Services>{
            result.data,
            Clock::now()
        };

        return result;
    }

    if (
        servicesCache_ &&
        Clock::now() - servicesCache_->storedAt <
            config_.maxCacheAge &&
        fallbackAllowed(
            result.networkStatus,
            result.httpStatus
        )
    )
    {
        result.source =
            OttMetadataSource::StaleCache;

        result.data = servicesCache_->data;

        return result;
    }

    if (
        servicesCache_ &&
        Clock::now() - servicesCache_->storedAt >=
            config_.maxCacheAge
    )
    {
        servicesCache_.reset();
    }

    return result;
}

OttMetadataResult<std::vector<EpgEvent>>
OttMetadataManager::getEpg(
    std::uint16_t serviceId,
    const std::string& date,
    bool forceRefresh
)
{
    if (
        serviceId == 0 ||
        !OttRestClient::isValidDate(date)
    )
    {
        OttMetadataResult<Events> result;

        result.networkStatus =
            OttRestStatus::InvalidRequest;

        result.errorMessage =
            "Invalid service ID or EPG date";

        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto now = Clock::now();

    pruneExpiredEpgLocked(now);

    const EpgKey key{serviceId, date};

    const auto cached = epgCache_.find(key);

    if (
        !forceRefresh &&
        cached != epgCache_.end() &&
        now - cached->second.storedAt <
            config_.epgTtl
    )
    {
        return cachedResult(cached->second);
    }

    auto result = fetchWithRetry<Events>(
        [this, serviceId, &date]()
        {
            return client_.fetchEpg(
                serviceId,
                date
            );
        }
    );

    if (result.success())
    {
        epgCache_.insert_or_assign(
            key,
            CacheEntry<Events>{
                result.data,
                Clock::now()
            }
        );

        enforceEpgLimitLocked();

        return result;
    }

    if (
        cached != epgCache_.end() &&
        Clock::now() - cached->second.storedAt <
            config_.maxCacheAge &&
        fallbackAllowed(
            result.networkStatus,
            result.httpStatus
        )
    )
    {
        result.source =
            OttMetadataSource::StaleCache;

        result.data = cached->second.data;

        return result;
    }

    if (
        result.networkStatus ==
            OttRestStatus::HttpError &&
        result.httpStatus == 404
    )
    {
        epgCache_.erase(key);
    }

    pruneExpiredEpgLocked(Clock::now());

    return result;
}

void OttMetadataManager::pruneExpiredEpgLocked(
    Clock::time_point now
)
{
    auto iterator = epgCache_.begin();

    while (iterator != epgCache_.end())
    {
        if (
            now - iterator->second.storedAt >=
                config_.maxCacheAge
        )
        {
            iterator = epgCache_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

void OttMetadataManager::enforceEpgLimitLocked()
{
    while (
        epgCache_.size() >
            config_.maxEpgEntries
    )
    {
        const auto oldest = std::min_element(
            epgCache_.begin(),
            epgCache_.end(),

            [](const auto& first,
               const auto& second)
            {
                return first.second.storedAt <
                       second.second.storedAt;
            }
        );

        epgCache_.erase(oldest);
    }
}

void OttMetadataManager::clearCache()
{
    std::lock_guard<std::mutex> lock(mutex_);

    servicesCache_.reset();

    epgCache_.clear();
}

std::size_t
OttMetadataManager::epgCacheSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return epgCache_.size();
}
