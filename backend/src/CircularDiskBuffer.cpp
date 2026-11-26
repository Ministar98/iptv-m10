#include "CircularDiskBuffer.hpp"

#include <iostream>
#include <limits>
#include <system_error>

namespace fs = std::filesystem;

CircularDiskBuffer::CircularDiskBuffer(
    const BufferConfig& config
)
    : config_(config)
{
}

bool CircularDiskBuffer::initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_)
    {
        return true;
    }

    if (config_.storageDirectory.empty() ||
        config_.maxDurationMs == 0 ||
        config_.maxStorageBytes == 0 ||
        config_.deletionGrace.count() < 0 ||
        (config_.deletionMode == SegmentDeletionMode::Deferred &&
         config_.maxTotalStorageBytes < config_.maxStorageBytes))
    {
        std::cerr
            << "Invalid circular buffer configuration\n";

        return false;
    }

    std::error_code error;

    fs::create_directories(
        config_.storageDirectory,
        error
    );

    if (error)
    {
        std::cerr
            << "Failed to create buffer directory: "
            << error.message() << '\n';

        return false;
    }

    const auto canonicalDirectory =
        fs::canonical(
            config_.storageDirectory,
            error
        );

    if (error)
    {
        return false;
    }

    config_.storageDirectory =
        canonicalDirectory;

    initialized_ = true;

    return true;
}

bool CircularDiskBuffer::addSegment(
    const SegmentMetadata& segment
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_)
    {
        return false;
    }

    if (segment.filePath.empty() ||
        segment.durationMs == 0 ||
        segment.durationMs > config_.maxDurationMs)
    {
        return false;
    }

    // Prevent timestamp overflow.

    constexpr auto maxTimestamp =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()
        );

    if (segment.startTimeMs > maxTimestamp ||
        segment.durationMs >
            maxTimestamp - segment.startTimeMs)
    {
        return false;
    }

    std::error_code error;

    const auto fileStatus =
        fs::symlink_status(
            segment.filePath,
            error
        );

    if (error ||
        !fs::is_regular_file(fileStatus))
    {
        return false;
    }

    const auto canonicalPath =
        fs::canonical(
            segment.filePath,
            error
        );

    if (error)
    {
        return false;
    }

    // Only files directly inside the configured
    // storage directory may be registered.

    if (canonicalPath.parent_path() !=
            config_.storageDirectory ||
        canonicalPath.extension() != ".ts")
    {
        return false;
    }

    const auto actualSize =
        fs::file_size(
            canonicalPath,
            error
        );

    if (error ||
        actualSize == 0 ||
        actualSize > config_.maxStorageBytes)
    {
        return false;
    }

    if (!segments_.empty())
    {
        const auto& previous =
            segments_.back();

        if (previous.sequenceNumber ==
                std::numeric_limits<
                    std::uint64_t
                >::max() ||
            segment.sequenceNumber !=
                previous.sequenceNumber + 1)
        {
            return false;
        }

        // Media timeline must remain continuous.

        if (segment.startTimeMs !=
                previous.endTimeMs())
        {
            return false;
        }
    }

    // Prevent registering the same file twice.

    for (const auto& existing : segments_)
    {
        if (existing.filePath == canonicalPath)
        {
            return false;
        }
    }

    for (const auto& retired : retired_)
    {
        if (retired.segment.filePath == canonicalPath)
            return false;
    }

    SegmentMetadata metadata = segment;

    metadata.filePath = canonicalPath;

    // Use the actual file size instead of trusting
    // the supplied metadata.

    metadata.sizeBytes =
        static_cast<std::uint64_t>(
            actualSize
        );

    if (config_.deletionMode == SegmentDeletionMode::Deferred &&
        (actualSize > config_.maxTotalStorageBytes ||
         storageBytes_ + retiredBytes_ > config_.maxTotalStorageBytes - actualSize))
    {
        return false;
    }

    // Make room for the incoming segment.

    while (!segments_.empty())
    {
        const std::uint64_t futureDuration =
            metadata.endTimeMs() -
            segments_.front().startTimeMs;

        const bool durationExceeded =
            futureDuration >
                config_.maxDurationMs;

        const bool storageExceeded =
            storageBytes_ >
                config_.maxStorageBytes -
                metadata.sizeBytes;

        if (!durationExceeded &&
            !storageExceeded)
        {
            break;
        }

        if (!removeOldestSegmentLocked())
        {
            return false;
        }
    }

    segments_.push_back(metadata);

    storageBytes_ += metadata.sizeBytes;

    ++totalAddedSegments_;

    return enforceLimitsLocked();
}

bool CircularDiskBuffer::removeOldestSegmentLocked()
{
    if (segments_.empty())
    {
        return false;
    }

    const auto& oldest =
        segments_.front();

    if (config_.deletionMode == SegmentDeletionMode::Deferred)
    {
        retired_.push_back({oldest, std::nullopt});
        retiredBytes_ += oldest.sizeBytes;
    }
    else
    {
        std::error_code error;
        fs::remove(oldest.filePath, error);
        if (error)
            return false;
    }

    if (oldest.discontinuity)
        ++discontinuitySequence_;

    storageBytes_ -= oldest.sizeBytes;

    segments_.pop_front();

    ++totalRemovedSegments_;

    return true;
}

bool CircularDiskBuffer::enforceLimitsLocked()
{
    while (!segments_.empty())
    {
        const BufferInterval interval =
            intervalLocked();

        const bool durationExceeded =
            interval.durationMs >
                config_.maxDurationMs;

        const bool storageExceeded =
            storageBytes_ >
                config_.maxStorageBytes;

        if (!durationExceeded &&
            !storageExceeded)
        {
            return true;
        }

        if (!removeOldestSegmentLocked())
        {
            return false;
        }
    }

    return true;
}

BufferInterval CircularDiskBuffer::intervalLocked() const
{
    BufferInterval interval{};

    if (segments_.empty())
    {
        return interval;
    }

    interval.available = true;

    interval.startTimeMs =
        segments_.front().startTimeMs;

    interval.endTimeMs =
        segments_.back().endTimeMs();

    interval.durationMs =
        interval.endTimeMs -
        interval.startTimeMs;

    return interval;
}

bool CircularDiskBuffer::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_)
    {
        return false;
    }

    while (!segments_.empty())
    {
        if (!removeOldestSegmentLocked())
        {
            return false;
        }
    }

    // Reset requires a stopped producer and no active playback.
    while (!retired_.empty())
    {
        std::error_code error;
        fs::remove(retired_.front().segment.filePath, error);
        if (error)
            return false;
        retiredBytes_ -= retired_.front().segment.sizeBytes;
        retired_.pop_front();
    }
    discontinuitySequence_ = 0;
    storageBytes_ = 0;

    totalAddedSegments_ = 0;

    totalRemovedSegments_ = 0;

    return true;
}

bool CircularDiskBuffer::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return segments_.empty();
}

std::size_t CircularDiskBuffer::segmentCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return segments_.size();
}

BufferInterval CircularDiskBuffer::availableInterval() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return intervalLocked();
}

BufferStatistics CircularDiskBuffer::statistics() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    BufferStatistics stats{};

    stats.segmentCount =
        segments_.size();

    stats.storageBytes =
        storageBytes_;

    stats.durationMs =
        intervalLocked().durationMs;

    stats.totalAddedSegments =
        totalAddedSegments_;

    stats.totalRemovedSegments =
        totalRemovedSegments_;

    return stats;
}

std::optional<SegmentMetadata>
CircularDiskBuffer::oldestSegment() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (segments_.empty())
    {
        return std::nullopt;
    }

    return segments_.front();
}

std::optional<SegmentMetadata>
CircularDiskBuffer::newestSegment() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (segments_.empty())
    {
        return std::nullopt;
    }

    return segments_.back();
}

std::optional<std::uint64_t>
CircularDiskBuffer::clampPosition(
    std::int64_t positionMs
) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (segments_.empty())
    {
        return std::nullopt;
    }

    const BufferInterval interval =
        intervalLocked();

    if (positionMs < 0)
    {
        return interval.startTimeMs;
    }

    const auto position =
        static_cast<std::uint64_t>(
            positionMs
        );

    if (position < interval.startTimeMs)
    {
        return interval.startTimeMs;
    }

    if (position >= interval.endTimeMs)
    {
        return interval.endTimeMs - 1;
    }

    return position;
}

std::optional<SegmentMetadata>
CircularDiskBuffer::findSegment(
    std::int64_t positionMs
) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (segments_.empty())
    {
        return std::nullopt;
    }

    if (positionMs < 0)
    {
        return segments_.front();
    }

    const auto position =
        static_cast<std::uint64_t>(
            positionMs
        );

    if (position <
            segments_.front().startTimeMs)
    {
        return segments_.front();
    }

    if (position >=
            segments_.back().endTimeMs())
    {
        return segments_.back();
    }

    for (const auto& segment : segments_)
    {
        if (position >= segment.startTimeMs &&
            position < segment.endTimeMs())
        {
            return segment;
        }
    }

    return std::nullopt;
}

std::vector<SegmentMetadata>
CircularDiskBuffer::segments() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return std::vector<SegmentMetadata>(
        segments_.begin(),
        segments_.end()
    );
}

void CircularDiskBuffer::markPlaylistPublished()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + config_.deletionGrace;
    for (auto& retired : retired_)
        if (!retired.deleteAfter)
            retired.deleteAfter = deadline;
}

bool CircularDiskBuffer::collectExpiredSegments()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    while (!retired_.empty())
    {
        const auto& entry = retired_.front();
        if (!entry.deleteAfter || now < *entry.deleteAfter)
            break;
        std::error_code error;
        fs::remove(entry.segment.filePath, error);
        if (error)
            return false;
        retiredBytes_ -= retired_.front().segment.sizeBytes;
        retired_.pop_front();
    }
    return true;
}

std::size_t CircularDiskBuffer::pendingDeletionCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return retired_.size();
}

std::uint64_t CircularDiskBuffer::discontinuitySequence() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return discontinuitySequence_;
}

std::uint64_t CircularDiskBuffer::totalStorageBytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return storageBytes_ + retiredBytes_;
}
