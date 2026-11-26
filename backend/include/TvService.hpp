#pragma once

#include <cstdint>
#include <string>

struct TvService
{
    std::uint16_t serviceId = 0;

    unsigned int channelNumber = 0;

    std::string name;

    std::string description;

    std::string rtpAddress = "127.0.0.1";

    std::uint16_t rtpPort = 5004;
};
