#include "BoundedTsQueue.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

BoundedTsQueue::BoundedTsQueue(
    std::size_t maxBytes,
    std::size_t maxBlocks
)
    : maxBytes_(maxBytes),
      maxBlocks_(maxBlocks)
{
    if (maxBytes_ == 0 || maxBlocks_ == 0)
    {
        throw std::invalid_argument(
            "BoundedTsQueue limits must be greater than zero"
        );
    }
}

BoundedTsQueue::PushResult BoundedTsQueue::tryPush(
    TsDataBlock&& block
)
{
    const std::size_t bytes = block.size();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (statistics_.closed)
        {
            ++statistics_.rejectedClosedBlocks;
            ++statistics_.droppedBlocks;
            statistics_.droppedBytes += bytes;

            return PushResult::Closed;
        }

        if (block.empty() || bytes > maxBytes_)
        {
            ++statistics_.invalidBlocks;
            ++statistics_.droppedBlocks;
            statistics_.droppedBytes += bytes;

            pendingDiscontinuity_ = true;

            return PushResult::Invalid;
        }

        const bool blockLimitReached =
            queue_.size() >= maxBlocks_;

        const bool byteLimitReached =
            bytes > maxBytes_ - statistics_.queuedBytes;

        if (blockLimitReached || byteLimitReached)
        {
            ++statistics_.overflowBlocks;
            ++statistics_.droppedBlocks;
            statistics_.droppedBytes += bytes;

            pendingDiscontinuity_ = true;

            return PushResult::Full;
        }

        queue_.push_back(std::move(block));

        if (pendingDiscontinuity_)
        {
            queue_.back().discontinuity = true;
            pendingDiscontinuity_ = false;
        }

        ++statistics_.acceptedBlocks;
        statistics_.acceptedBytes += bytes;

        statistics_.queuedBlocks = queue_.size();
        statistics_.queuedBytes += bytes;

        statistics_.peakBlocks = std::max(
            statistics_.peakBlocks,
            statistics_.queuedBlocks
        );

        statistics_.peakBytes = std::max(
            statistics_.peakBytes,
            statistics_.queuedBytes
        );
    }

    condition_.notify_one();

    return PushResult::Accepted;
}

BoundedTsQueue::PopResult BoundedTsQueue::waitPop(
    TsDataBlock& block,
    std::chrono::milliseconds timeout
)
{
    std::unique_lock<std::mutex> lock(mutex_);

    const bool ready = condition_.wait_for(
        lock,
        timeout,
        [this]()
        {
            return !queue_.empty() || statistics_.closed;
        }
    );

    if (!ready)
    {
        return PopResult::Timeout;
    }

    if (queue_.empty())
    {
        return PopResult::Closed;
    }

    const std::size_t bytes = queue_.front().size();

    block = std::move(queue_.front());
    queue_.pop_front();

    ++statistics_.poppedBlocks;
    statistics_.poppedBytes += bytes;

    statistics_.queuedBlocks = queue_.size();
    statistics_.queuedBytes -= bytes;

    return PopResult::Data;
}

void BoundedTsQueue::close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        statistics_.closed = true;
    }

    condition_.notify_all();
}

void BoundedTsQueue::abort()
{
    std::deque<TsDataBlock> discarded;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        statistics_.closed = true;

        statistics_.discardedBlocks += queue_.size();
        statistics_.droppedBlocks += queue_.size();
        statistics_.droppedBytes += statistics_.queuedBytes;

        discarded.swap(queue_);

        statistics_.queuedBlocks = 0;
        statistics_.queuedBytes = 0;
    }

    condition_.notify_all();
}

BoundedTsQueue::Statistics BoundedTsQueue::statistics() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return statistics_;
}
