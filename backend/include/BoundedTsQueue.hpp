#pragma once

#include "TsDataBlock.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

class BoundedTsQueue
{
public:
    enum class PushResult
    {
        Accepted,
        Full,
        Invalid,
        Closed
    };

    enum class PopResult
    {
        Data,
        Timeout,
        Closed
    };

    struct Statistics
    {
        std::uint64_t acceptedBlocks = 0;
        std::uint64_t acceptedBytes = 0;

        std::uint64_t poppedBlocks = 0;
        std::uint64_t poppedBytes = 0;

        std::uint64_t droppedBlocks = 0;
        std::uint64_t droppedBytes = 0;

        std::uint64_t overflowBlocks = 0;
        std::uint64_t invalidBlocks = 0;
        std::uint64_t rejectedClosedBlocks = 0;
        std::uint64_t discardedBlocks = 0;

        std::size_t queuedBlocks = 0;
        std::size_t queuedBytes = 0;

        std::size_t peakBlocks = 0;
        std::size_t peakBytes = 0;

        bool closed = false;
    };

    explicit BoundedTsQueue(
        std::size_t maxBytes = 8U * 1024U * 1024U,
        std::size_t maxBlocks = 4096U
    );

    BoundedTsQueue(const BoundedTsQueue&) = delete;
    BoundedTsQueue& operator=(const BoundedTsQueue&) = delete;

    PushResult tryPush(TsDataBlock&& block);

    PopResult waitPop(
        TsDataBlock& block,
        std::chrono::milliseconds timeout
    );

    void close();

    void abort();

    Statistics statistics() const;

private:
    const std::size_t maxBytes_;
    const std::size_t maxBlocks_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;

    std::deque<TsDataBlock> queue_;

    Statistics statistics_;

    bool pendingDiscontinuity_ = false;
};
