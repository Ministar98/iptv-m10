#include "HlsConsumer.hpp"
#include "HlsPlaylistWriter.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <system_error>

HlsConsumer::HlsConsumer(
    BoundedTsQueue& queue,
    const HlsConfig& hlsConfig,
    const BufferConfig& bufferConfig
)
    : queue_(queue),
      hlsConfig_(hlsConfig),
      manager_(bufferConfig)
{
}

bool HlsConsumer::initialize()
{
    if (initialized_ || hasFatalError() || isFinished())
    {
        return false;
    }

    if (hlsConfig_.outputDirectory.empty() ||
        hlsConfig_.playlistFilename != "producer.m3u8" ||
        hlsConfig_.deleteSegments)
    {
        fail("Invalid HLS consumer configuration");

        return false;
    }

    std::error_code error;

    std::filesystem::remove_all(
        hlsConfig_.outputDirectory,
        error
    );

    if (error)
    {
        fail(
            "Failed to clean HLS directory: " +
            error.message()
        );

        return false;
    }

    if (!manager_.initialize())
    {
        fail("Failed to initialize HLS buffer manager");

        return false;
    }

    if (!segmenter_.start(hlsConfig_))
    {
        fail("Failed to start HLS segmenter");

        return false;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);

        publishedInterval_ = BufferInterval{};
    }

    initialized_ = true;

    refreshBufferStatistics();

    return true;
}

void HlsConsumer::run()
{
    if (!initialized_)
    {
        fail("HLS consumer is not initialized");

        finished_.store(true);

        return;
    }

    std::cout << "[INFO] HLS consumer started\n";

    try
    {
        auto lastBufferUpdate =
            std::chrono::steady_clock::now();

        while (!hasFatalError())
        {
            if (!segmenter_.isRunning())
            {
                {
                    std::lock_guard<std::mutex>
                        lock(stateMutex_);

                    ++statistics_.processErrors;
                }

                fail("FFmpeg process stopped unexpectedly");

                break;
            }

            const auto now =
                std::chrono::steady_clock::now();

            if (
                now - lastBufferUpdate >=
                BUFFER_UPDATE_INTERVAL
            )
            {
                lastBufferUpdate = now;

                if (!updateBuffer())
                {
                    break;
                }
            }

            TsDataBlock block;

            const auto result =
                queue_.waitPop(
                    block,
                    QUEUE_TIMEOUT
                );

            if (
                result ==
                BoundedTsQueue::PopResult::Closed
            )
            {
                break;
            }

            if (
                result ==
                BoundedTsQueue::PopResult::Timeout
            )
            {
                std::lock_guard<std::mutex>
                    lock(stateMutex_);

                ++statistics_.queueTimeouts;

                continue;
            }

            if (!processBlock(block))
            {
                break;
            }
        }
    }
    catch (const std::exception& error)
    {
        fail(
            "HLS consumer exception: " +
            std::string(error.what())
        );
    }
    catch (...)
    {
        fail("Unknown HLS consumer exception");
    }

    segmenter_.stop();

    if (!hasFatalError())
    {
        try
        {
            updateBuffer();
        }
        catch (const std::exception& error)
        {
            fail(
                "Final HLS update exception: " +
                std::string(error.what())
            );
        }
        catch (...)
        {
            fail("Unknown final HLS update exception");
        }
    }

    initialized_ = false;

    finished_.store(true);

    std::cout << "[INFO] HLS consumer stopped\n";
}

bool HlsConsumer::processBlock(
    const TsDataBlock& block
)
{
    {
        std::lock_guard<std::mutex>
            lock(stateMutex_);

        ++statistics_.consumedBlocks;

        statistics_.consumedBytes += block.size();

        if (block.discontinuity)
        {
            ++statistics_.discontinuities;
        }
    }

    if (block.empty() || block.size() % 188 != 0)
    {
        fail("Invalid MPEG-TS block received by consumer");

        return false;
    }

    if (!segmenter_.write(
            block.data.data(),
            block.size()
        ))
    {
        {
            std::lock_guard<std::mutex>
                lock(stateMutex_);

            ++statistics_.writeErrors;
        }

        fail("Failed to forward MPEG-TS to FFmpeg");

        return false;
    }

    {
        std::lock_guard<std::mutex>
            lock(stateMutex_);

        ++statistics_.forwardedBlocks;

        statistics_.forwardedBytes += block.size();
    }

    return true;
}

bool HlsConsumer::updateBuffer()
{
    if (!manager_.collectExpiredSegments())
    {
        {
            std::lock_guard<std::mutex>
                lock(stateMutex_);

            ++statistics_.bufferUpdateErrors;
        }

        fail("Failed to collect expired HLS segments");

        return false;
    }

    const auto result = manager_.update();

    if (
        result.status ==
        HlsUpdateStatus::PlaylistUnavailable
    )
    {
        refreshBufferStatistics();

        return true;
    }

    if (
        result.status != HlsUpdateStatus::Updated &&
        result.status != HlsUpdateStatus::NoChanges
    )
    {
        {
            std::lock_guard<std::mutex>
                lock(stateMutex_);

            ++statistics_.bufferUpdateErrors;
        }

        fail(
            "HLS buffer update failed: " +
            std::to_string(
                static_cast<int>(result.status)
            )
        );

        return false;
    }

    if (result.addedSegments > 0 || result.endList)
    {
        const auto segments =
            manager_.buffer().segments();

        BufferInterval interval{};

        if (!segments.empty())
        {
            interval.available = true;

            interval.startTimeMs =
                segments.front().startTimeMs;

            interval.endTimeMs =
                segments.back().endTimeMs();

            interval.durationMs =
                interval.endTimeMs -
                interval.startTimeMs;
        }

        const bool published =
            HlsPlaylistWriter::write(
                hlsConfig_.outputDirectory /
                    "index.m3u8",
                segments,
                hlsConfig_.segmentDurationSeconds,
                true,
                result.endList,
                manager_.buffer()
                    .discontinuitySequence()
            );

        if (!published)
        {
            {
                std::lock_guard<std::mutex>
                    lock(stateMutex_);

                ++statistics_.bufferUpdateErrors;
            }

            fail("Failed to publish public HLS playlist");

            return false;
        }

        manager_.markPlaylistPublished();

        {
            std::lock_guard<std::mutex>
                lock(stateMutex_);

            publishedInterval_ = interval;

            ++statistics_.playlistUpdates;

            statistics_.registeredSegments +=
                result.addedSegments;
        }
    }

    refreshBufferStatistics();

    return true;
}

void HlsConsumer::refreshBufferStatistics()
{
    const auto bufferStats =
        manager_.buffer().statistics();

    const auto pendingDeletion =
        manager_.buffer().pendingDeletionCount();

    const auto totalStorage =
        manager_.buffer().totalStorageBytes();

    std::lock_guard<std::mutex>
        lock(stateMutex_);

    statistics_.buffer = bufferStats;

    statistics_.pendingDeletionCount =
        pendingDeletion;

    statistics_.totalStorageBytes =
        totalStorage;
}

void HlsConsumer::fail(
    const std::string& message
)
{
    {
        std::lock_guard<std::mutex>
            lock(stateMutex_);

        if (lastError_.empty())
        {
            lastError_ = message;
        }
    }

    fatalError_.store(true);

    queue_.abort();

    std::cerr
        << "[ERROR] HLS consumer: "
        << message << '\n';
}

bool HlsConsumer::hasFatalError() const noexcept
{
    return fatalError_.load();
}

bool HlsConsumer::isFinished() const noexcept
{
    return finished_.load();
}

HlsConsumerStatistics HlsConsumer::statistics() const
{
    std::lock_guard<std::mutex>
        lock(stateMutex_);

    return statistics_;
}

BufferInterval HlsConsumer::publishedInterval() const
{
    std::lock_guard<std::mutex>
        lock(stateMutex_);

    return publishedInterval_;
}

std::string HlsConsumer::lastError() const
{
    std::lock_guard<std::mutex>
        lock(stateMutex_);

    return lastError_;
}
