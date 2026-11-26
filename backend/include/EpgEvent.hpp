#pragma once

#include <cstdint>
#include <string>

struct EpgEvent
{
    std::string eventId;

    std::uint16_t serviceId = 0;

    std::string title;

    std::string description;

    std::string startTime;

    std::string endTime;

    std::int64_t startTimeMs = 0;

    std::int64_t endTimeMs = 0;
};
