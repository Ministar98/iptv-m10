#pragma once

#include "TvService.hpp"
#include "EpgEvent.hpp"
#include "OttRestResult.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct OttRestConfig
{
    std::string baseUrl = "http://127.0.0.1:8090";

    long connectTimeoutMs = 1000;

    long requestTimeoutMs = 2000;

    std::size_t maxResponseBytes = 1024 * 1024;
};

class OttRestClient
{
public:
    explicit OttRestClient(
        const OttRestConfig& config = OttRestConfig{}
    );

    OttRestResult<std::vector<TvService>>
    fetchServices() const;

    OttRestResult<std::vector<EpgEvent>>
    fetchEpg(
        std::uint16_t serviceId,
        const std::string& date
    ) const;

    static bool isValidDate(
        const std::string& date
    );

    static std::string currentUtcDate();

private:
    const OttRestConfig config_;

    OttRestResult<std::string> get(
        const std::string& path
    ) const;
};
