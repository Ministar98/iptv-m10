#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

struct TsDataBlock
{
    std::vector<std::uint8_t> data;

    std::uint16_t rtpSequenceNumber = 0;
    std::uint32_t rtpTimestamp = 0;

    bool discontinuity = false;

    std::chrono::steady_clock::time_point receivedAt{};

    std::size_t size() const noexcept
    {
        return data.size();
    }

    bool empty() const noexcept
    {
        return data.empty();
    }
};
