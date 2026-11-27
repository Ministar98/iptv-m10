#include "EventQueue.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

EventQueue::EventQueue(
    std::size_t capacity
)
    : capacity_(capacity)
{
    if (capacity_ == 0)
    {
        throw std::invalid_argument(
            "Event queue capacity must be positive"
        );
    }
}

bool EventQueue::publish(
    ApiJson::Json event
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (statistics_.closed)
    {
        ++statistics_.rejectedClosed;

        return false;
    }

    events_.push_back(std::move(event));

    ++statistics_.published;

    if (events_.size() > capacity_)
    {
        events_.pop_front();

        ++statistics_.dropped;
    }

    statistics_.queued = events_.size();

    statistics_.peakQueued = std::max(
        statistics_.peakQueued,
        statistics_.queued
    );

    return true;
}

std::optional<ApiJson::Json>
EventQueue::tryPop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (events_.empty())
    {
        return std::nullopt;
    }

    auto event = std::move(events_.front());

    events_.pop_front();

    ++statistics_.consumed;

    statistics_.queued = events_.size();

    return event;
}

void EventQueue::close()
{
    std::lock_guard<std::mutex> lock(mutex_);

    statistics_.closed = true;
}

std::size_t EventQueue::capacity() const noexcept
{
    return capacity_;
}

EventQueueStatistics EventQueue::statistics() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return statistics_;
}
