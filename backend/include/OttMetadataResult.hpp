#pragma once

#include "OttRestResult.hpp"

#include <cstddef>

enum class OttMetadataSource
{
    None,
    Network,
    Cache,
    StaleCache
};

inline constexpr const char* ottMetadataSourceToString(
    OttMetadataSource source
) noexcept
{
    switch (source)
    {
        case OttMetadataSource::None:
            return "NONE";

        case OttMetadataSource::Network:
            return "NETWORK";

        case OttMetadataSource::Cache:
            return "CACHE";

        case OttMetadataSource::StaleCache:
            return "STALE_CACHE";
    }

    return "UNKNOWN";
}

template<typename T>
struct OttMetadataResult
{
    OttMetadataSource source =
        OttMetadataSource::None;

    OttRestStatus networkStatus =
        OttRestStatus::Success;

    long httpStatus = 0;

    std::size_t attempts = 0;

    T data{};

    std::string errorMessage;

    bool success() const noexcept
    {
        return source != OttMetadataSource::None;
    }

    bool fromCache() const noexcept
    {
        return source == OttMetadataSource::Cache ||
               source == OttMetadataSource::StaleCache;
    }

    bool stale() const noexcept
    {
        return source == OttMetadataSource::StaleCache;
    }
};
