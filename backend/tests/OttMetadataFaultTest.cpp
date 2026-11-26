#include "OttMetadataManager.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

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

OttRestConfig restConfig(
    const std::string& baseUrl,
    const std::string& mode
)
{
    OttRestConfig config;

    config.baseUrl = baseUrl + "/" + mode;
    config.connectTimeoutMs = 100;
    config.requestTimeoutMs = 200;

    return config;
}

OttMetadataConfig metadataConfig()
{
    OttMetadataConfig config;

    config.maxAttempts = 3;
    config.initialBackoff = 50ms;
    config.maxBackoff = 200ms;

    config.servicesTtl = 60000ms;
    config.epgTtl = 60000ms;
    config.maxCacheAge = 120000ms;

    return config;
}

void testTimeout(const std::string& baseUrl)
{
    OttMetadataManager manager(
        restConfig(baseUrl, "timeout"),
        metadataConfig()
    );

    const auto start =
        std::chrono::steady_clock::now();

    const auto result = manager.getServices();

    const auto elapsed =
        std::chrono::steady_clock::now() - start;

    require(
        !result.success() &&
        result.networkStatus ==
            OttRestStatus::Timeout &&
        result.attempts == 3,
        "Timeout retry failed"
    );

    require(
        elapsed >= 700ms &&
        elapsed < 3000ms,
        "Unexpected timeout duration"
    );

    pass("Request timeout and bounded retry");
}

void testUnavailable(const std::string& baseUrl)
{
    OttMetadataManager manager(
        restConfig(baseUrl, "unavailable"),
        metadataConfig()
    );

    const auto result = manager.getServices();

    require(
        !result.success() &&
        result.networkStatus ==
            OttRestStatus::HttpError &&
        result.httpStatus == 503 &&
        result.attempts == 3,
        "HTTP 503 retry failed"
    );

    pass("HTTP 503 retry");
}

void testNotFound(const std::string& baseUrl)
{
    OttMetadataManager manager(
        restConfig(baseUrl, "notfound"),
        metadataConfig()
    );

    const auto result = manager.getServices();

    require(
        !result.success() &&
        result.networkStatus ==
            OttRestStatus::HttpError &&
        result.httpStatus == 404 &&
        result.attempts == 1,
        "HTTP 404 handling failed"
    );

    pass("HTTP 404 without retry");
}

void testInvalidResponse(
    const std::string& baseUrl,
    const std::string& mode
)
{
    OttMetadataManager manager(
        restConfig(baseUrl, mode),
        metadataConfig()
    );

    const auto result = manager.getServices();

    require(
        !result.success() &&
        result.networkStatus ==
            OttRestStatus::InvalidResponse &&
        result.attempts == 1,
        "Invalid response handling failed: " + mode
    );

    pass("Invalid response: " + mode);
}

void testRecovery(const std::string& baseUrl)
{
    OttMetadataManager manager(
        restConfig(baseUrl, "recovery"),
        metadataConfig()
    );

    const auto start =
        std::chrono::steady_clock::now();

    const auto result = manager.getServices();

    const auto elapsed =
        std::chrono::steady_clock::now() - start;

    require(
        result.success() &&
        result.source ==
            OttMetadataSource::Network &&
        result.attempts == 3 &&
        result.httpStatus == 200 &&
        result.data.size() == 1,
        "Transient HTTP error recovery failed"
    );

    require(
        elapsed >= 150ms,
        "Recovery backoff missing"
    );

    pass("Recovery after two HTTP 503 responses");
}

void testCacheProtection(const std::string& baseUrl)
{
    OttMetadataManager manager(
        restConfig(baseUrl, "cache"),
        metadataConfig()
    );

    const auto initial = manager.getServices();

    require(
        initial.success() &&
        initial.source ==
            OttMetadataSource::Network &&
        initial.data.size() == 1,
        "Initial cache population failed"
    );

    pass("Initial valid cache population");

    const auto invalid = manager.getServices(true);

    require(
        invalid.success() &&
        invalid.stale() &&
        invalid.networkStatus ==
            OttRestStatus::InvalidResponse &&
        invalid.attempts == 1 &&
        invalid.data.size() == 1 &&
        invalid.data.front().name == "Channel 1",
        "Invalid response damaged cache"
    );

    pass("Cache protected from invalid JSON");

    const auto cached = manager.getServices();

    require(
        cached.success() &&
        cached.source ==
            OttMetadataSource::Cache &&
        cached.attempts == 0 &&
        cached.data.size() == 1,
        "Valid cached catalog was lost"
    );

    pass("Valid cache preserved");

    const auto recovered = manager.getServices(true);

    require(
        recovered.success() &&
        recovered.source ==
            OttMetadataSource::Network &&
        recovered.attempts == 1,
        "Cache recovery failed"
    );

    pass("Cache refresh after recovery");
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr
            << "Usage: ott_metadata_fault_test BASE_URL\n";

        return 1;
    }

    try
    {
        const std::string baseUrl = argv[1];

        testTimeout(baseUrl);
        testUnavailable(baseUrl);
        testNotFound(baseUrl);

        testInvalidResponse(baseUrl, "invalidjson");
        testInvalidResponse(baseUrl, "invalidschema");

        testRecovery(baseUrl);
        testCacheProtection(baseUrl);

        std::cout
            << "\nAll OTT fault tests passed.\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "[FAIL] " << error.what() << '\n';

        return 1;
    }
}
