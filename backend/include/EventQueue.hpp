#pragma once

#include "ApiJson.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

struct EventQueueStatistics
{
    std::uint64_t published = 0;
    std::uint64_t consumed = 0;
    std::uint64_t dropped = 0;
    std::uint64_t rejectedClosed = 0;

    std::size_t queued = 0;
    std::size_t peakQueued = 0;

    bool closed = false;
};

class EventQueue
{
public:
    explicit EventQueue(
        std::size_t capacity = 256
    );

    EventQueue(const EventQueue&) = delete;

    EventQueue& operator=(const EventQueue&) = delete;

    bool publish(ApiJson::Json event);

    std::optional<ApiJson::Json> tryPop();

    void close();

    std::size_t capacity() const noexcept;

    EventQueueStatistics statistics() const;

private:
    const std::size_t capacity_;

    mutable std::mutex mutex_;

    std::deque<ApiJson::Json> events_;

    EventQueueStatistics statistics_{};
};
