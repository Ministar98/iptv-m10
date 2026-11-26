#pragma once

#include "BoundedTsQueue.hpp"
#include "HlsSegmenter.hpp"
#include "HlsBufferManager.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

struct HlsConsumerStatistics
{
    std::uint64_t consumedBlocks = 0;
    std::uint64_t consumedBytes = 0;

    std::uint64_t forwardedBlocks = 0;
    std::uint64_t forwardedBytes = 0;

    std::uint64_t discontinuities = 0;
    std::uint64_t queueTimeouts = 0;

    std::uint64_t writeErrors = 0;
    std::uint64_t bufferUpdateErrors = 0;
    std::uint64_t processErrors = 0;

    std::uint64_t playlistUpdates = 0;
    std::uint64_t registeredSegments = 0;

    BufferStatistics buffer{};

    std::size_t pendingDeletionCount = 0;
    std::uint64_t totalStorageBytes = 0;
};

class HlsConsumer
{
public:
    HlsConsumer(
        BoundedTsQueue& queue,
        const HlsConfig& hlsConfig,
        const BufferConfig& bufferConfig
    );

    HlsConsumer(const HlsConsumer&) = delete;
    HlsConsumer& operator=(const HlsConsumer&) = delete;

    // Must be called before worker threads are started.
    bool initialize();

    // Executed exclusively by the consumer thread.
    void run();

    bool hasFatalError() const noexcept;

    bool isFinished() const noexcept;

    HlsConsumerStatistics statistics() const;

    std::string lastError() const;

private:
    static constexpr std::chrono::milliseconds
        QUEUE_TIMEOUT{100};

    static constexpr std::chrono::milliseconds
        BUFFER_UPDATE_INTERVAL{250};

    BoundedTsQueue& queue_;

    HlsConfig hlsConfig_;

    HlsSegmenter segmenter_;
    HlsBufferManager manager_;

    bool initialized_ = false;

    std::atomic<bool> fatalError_{false};
    std::atomic<bool> finished_{false};

    mutable std::mutex stateMutex_;

    HlsConsumerStatistics statistics_{};

    std::string lastError_;

    bool updateBuffer();

    bool processBlock(const TsDataBlock& block);

    void refreshBufferStatistics();

    void fail(const std::string& message);
};
