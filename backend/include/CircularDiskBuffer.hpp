#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

enum class SegmentDeletionMode { Immediate, Deferred };

struct BufferConfig
{
    std::filesystem::path storageDirectory =
        "runtime/hls";

    std::uint64_t maxDurationMs =
        30ULL * 60ULL * 1000ULL;

    std::uint64_t maxStorageBytes =
        15ULL * 1024ULL * 1024ULL * 1024ULL;

    SegmentDeletionMode deletionMode = SegmentDeletionMode::Immediate;
    std::chrono::milliseconds deletionGrace{30000};
    std::uint64_t maxTotalStorageBytes = 20ULL * 1024 * 1024 * 1024;
};

struct SegmentMetadata
{
    std::uint64_t sequenceNumber = 0;

    std::filesystem::path filePath;

    std::uint64_t startTimeMs = 0;

    std::uint64_t durationMs = 0;

    std::uint64_t sizeBytes = 0;

    bool discontinuity = false;

    std::uint64_t endTimeMs() const
    {
        return startTimeMs + durationMs;
    }
};

struct BufferInterval
{
    bool available = false;

    std::uint64_t startTimeMs = 0;

    std::uint64_t endTimeMs = 0;

    std::uint64_t durationMs = 0;
};

struct BufferStatistics
{
    std::size_t segmentCount = 0;

    std::uint64_t storageBytes = 0;

    std::uint64_t durationMs = 0;

    std::uint64_t totalAddedSegments = 0;

    std::uint64_t totalRemovedSegments = 0;
};

class CircularDiskBuffer
{
public:
    explicit CircularDiskBuffer(
        const BufferConfig& config
    );

    ~CircularDiskBuffer() = default;

    CircularDiskBuffer(
        const CircularDiskBuffer&
    ) = delete;

    CircularDiskBuffer& operator=(
        const CircularDiskBuffer&
    ) = delete;

    bool initialize();

    bool addSegment(
        const SegmentMetadata& segment
    );

    bool reset();

    // Call only after successful publication of the current snapshot.
    void markPlaylistPublished();
    bool collectExpiredSegments();
    std::size_t pendingDeletionCount() const;
    std::uint64_t totalStorageBytes() const;
    std::uint64_t discontinuitySequence() const;

    bool empty() const;

    std::size_t segmentCount() const;

    BufferInterval availableInterval() const;

    BufferStatistics statistics() const;

    std::optional<SegmentMetadata>
    oldestSegment() const;

    std::optional<SegmentMetadata>
    newestSegment() const;

    std::optional<SegmentMetadata>
    findSegment(
        std::int64_t positionMs
    ) const;

    std::optional<std::uint64_t>
    clampPosition(
        std::int64_t positionMs
    ) const;

    std::vector<SegmentMetadata>
    segments() const;

private:
    BufferConfig config_;

    mutable std::mutex mutex_;

    std::deque<SegmentMetadata> segments_;

    struct RetiredSegment
    {
        SegmentMetadata segment;
        std::optional<std::chrono::steady_clock::time_point> deleteAfter;
    };
    std::deque<RetiredSegment> retired_;
    std::uint64_t discontinuitySequence_ = 0;
    bool initialized_ = false;

    std::uint64_t storageBytes_ = 0;
    std::uint64_t retiredBytes_ = 0;

    std::uint64_t totalAddedSegments_ = 0;

    std::uint64_t totalRemovedSegments_ = 0;

    bool enforceLimitsLocked();

    bool removeOldestSegmentLocked();

    BufferInterval intervalLocked() const;
};
