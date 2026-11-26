#include "OttMetadataManager.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{

using namespace std::chrono_literals;

void require(
    bool condition,
    const std::string& message
)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void pass(const std::string& message)
{
    std::cout << "[PASS] " << message << '\n';
}

void waitForUser(const std::string& message)
{
    std::cout
        << "\n" << message
        << "\nPress ENTER to continue..."
        << std::endl;

    std::string input;

    if (!std::getline(std::cin, input))
    {
        throw std::runtime_error(
            "Input unavailable"
        );
    }
}

void testMetadata()
{
    OttRestConfig restConfig;

    restConfig.connectTimeoutMs = 500;
    restConfig.requestTimeoutMs = 1000;

    OttMetadataConfig config;

    config.maxAttempts = 3;
    config.initialBackoff = 100ms;
    config.maxBackoff = 400ms;

    config.servicesTtl = 60000ms;
    config.epgTtl = 60000ms;
    config.maxCacheAge = 600000ms;

    config.maxEpgEntries = 2;

    OttMetadataManager manager(
        restConfig,
        config
    );

    const auto date =
        OttRestClient::currentUtcDate();

    const auto services =
        manager.getServices();

    require(
        services.success() &&
        services.source ==
            OttMetadataSource::Network &&
        services.attempts == 1 &&
        services.data.size() == 3,
        "Initial catalog request failed"
    );

    pass("Initial catalog request");

    const auto cachedServices =
        manager.getServices();

    require(
        cachedServices.success() &&
        cachedServices.fromCache() &&
        !cachedServices.stale() &&
        cachedServices.attempts == 0 &&
        cachedServices.data.size() == 3,
        "Catalog cache failed"
    );

    pass("Catalog cache hit");

    const auto refreshedServices =
        manager.getServices(true);

    require(
        refreshedServices.success() &&
        refreshedServices.source ==
            OttMetadataSource::Network &&
        refreshedServices.attempts == 1,
        "Forced catalog refresh failed"
    );

    pass("Forced catalog refresh");

    const auto epg =
        manager.getEpg(1, date);

    require(
        epg.success() &&
        epg.source ==
            OttMetadataSource::Network &&
        !epg.data.empty(),
        "Initial EPG request failed"
    );

    pass("Initial EPG request");

    const auto cachedEpg =
        manager.getEpg(1, date);

    require(
        cachedEpg.success() &&
        cachedEpg.fromCache() &&
        cachedEpg.attempts == 0 &&
        cachedEpg.data.size() ==
            epg.data.size(),
        "EPG cache failed"
    );

    pass("EPG cache hit");

    require(
        manager.getEpg(2, date).success(),
        "Channel 2 EPG failed"
    );

    require(
        manager.getEpg(3, date).success(),
        "Channel 3 EPG failed"
    );

    require(
        manager.epgCacheSize() == 2,
        "EPG cache limit exceeded"
    );

    pass("EPG cache capacity");

    const auto evicted =
        manager.getEpg(1, date);

    require(
        evicted.success() &&
        evicted.source ==
            OttMetadataSource::Network,
        "Oldest EPG entry was not evicted"
    );

    pass("Oldest EPG entry eviction");

    const auto invalid =
        manager.getEpg(0, date);

    require(
        !invalid.success() &&
        invalid.networkStatus ==
            OttRestStatus::InvalidRequest &&
        invalid.attempts == 0,
        "Invalid request was not rejected"
    );

    pass("Invalid request without retry");

    const auto unknown =
        manager.getEpg(99, date);

    require(
        !unknown.success() &&
        unknown.networkStatus ==
            OttRestStatus::HttpError &&
        unknown.httpStatus == 404 &&
        unknown.attempts == 1,
        "HTTP 404 handling failed"
    );

    pass("HTTP 404 without retry");

    waitForUser("STOP the OTT server.");

    const auto offlineCached =
        manager.getServices();

    require(
        offlineCached.success() &&
        offlineCached.source ==
            OttMetadataSource::Cache &&
        offlineCached.attempts == 0,
        "Offline cache access failed"
    );

    pass("Offline fresh cache access");

    const auto start =
        std::chrono::steady_clock::now();

    const auto offlineServices =
        manager.getServices(true);

    const auto elapsed =
        std::chrono::steady_clock::now() - start;

    require(
        offlineServices.success() &&
        offlineServices.stale() &&
        offlineServices.attempts == 3 &&
        offlineServices.networkStatus ==
            OttRestStatus::ConnectionError &&
        offlineServices.data.size() == 3,
        "Catalog fallback failed"
    );

    require(
        elapsed >= 300ms,
        "Retry backoff was not applied"
    );

    pass("Three attempts with exponential backoff");

    pass("Stale catalog fallback");

    const auto offlineEpg =
        manager.getEpg(1, date, true);

    require(
        offlineEpg.success() &&
        offlineEpg.stale() &&
        offlineEpg.attempts == 3 &&
        offlineEpg.data.size() ==
            epg.data.size(),
        "EPG fallback failed"
    );

    pass("Stale EPG fallback");

    const auto uncachedEpg =
        manager.getEpg(2, "2028-01-01");

    require(
        !uncachedEpg.success() &&
        uncachedEpg.attempts == 3 &&
        uncachedEpg.source ==
            OttMetadataSource::None,
        "Unexpected uncached EPG result"
    );

    pass("Offline EPG without cache");

    OttMetadataManager emptyManager(
        restConfig,
        config
    );

    const auto emptyResult =
        emptyManager.getServices();

    require(
        !emptyResult.success() &&
        emptyResult.attempts == 3,
        "Unexpected success without cache"
    );

    pass("Offline startup without cache");

    waitForUser("START the OTT server again.");

    const auto recovered =
        manager.getServices(true);

    require(
        recovered.success() &&
        recovered.source ==
            OttMetadataSource::Network &&
        recovered.attempts == 1 &&
        recovered.data.size() == 3,
        "Catalog recovery failed"
    );

    pass("Catalog recovery");

    const auto recoveredEpg =
        manager.getEpg(1, date, true);

    require(
        recoveredEpg.success() &&
        recoveredEpg.source ==
            OttMetadataSource::Network,
        "EPG recovery failed"
    );

    pass("EPG recovery");

    manager.clearCache();

    require(
        manager.epgCacheSize() == 0,
        "Cache clearing failed"
    );

    require(
        manager.getServices().source ==
            OttMetadataSource::Network,
        "Catalog cache was not cleared"
    );

    pass("Cache clearing");
}

void testExpiration()
{
    OttMetadataConfig config;

    config.servicesTtl = 50ms;
    config.epgTtl = 50ms;
    config.maxCacheAge = 500ms;

    config.maxAttempts = 1;

    OttMetadataManager manager(
        OttRestConfig{},
        config
    );

    const auto date =
        OttRestClient::currentUtcDate();

    require(
        manager.getServices().success(),
        "Initial catalog failed"
    );

    std::this_thread::sleep_for(80ms);

    require(
        manager.getServices().source ==
            OttMetadataSource::Network,
        "Expired catalog was not refreshed"
    );

    pass("Catalog TTL expiration");

    require(
        manager.getEpg(1, date).success(),
        "Initial EPG failed"
    );

    std::this_thread::sleep_for(80ms);

    require(
        manager.getEpg(1, date).source ==
            OttMetadataSource::Network,
        "Expired EPG was not refreshed"
    );

    pass("EPG TTL expiration");

    waitForUser("STOP the OTT server again.");

    std::this_thread::sleep_for(600ms);

    require(
        !manager.getServices().success(),
        "Expired catalog fallback was accepted"
    );

    pass("Maximum catalog cache age");

    require(
        !manager.getEpg(1, date).success(),
        "Expired EPG fallback was accepted"
    );

    require(
        manager.epgCacheSize() == 0,
        "Expired EPG entry was not removed"
    );

    pass("Maximum EPG cache age");
}

} // namespace

int main()
{
    try
    {
        testMetadata();

        testExpiration();

        std::cout
            << "\nAll OTT Metadata Manager tests passed.\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "[FAIL] "
            << error.what()
            << '\n';

        return 1;
    }
}
