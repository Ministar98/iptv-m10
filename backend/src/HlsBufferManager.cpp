#include "HlsBufferManager.hpp"

#include <limits>
#include <system_error>

HlsBufferManager::HlsBufferManager(
    const BufferConfig& config
)
    : config_(config),
      buffer_(config),
      playlistPath_(
          config.storageDirectory /
          "producer.m3u8"
      )
{
}

bool HlsBufferManager::initialize()
{
    if (initialized_)
    {
        return true;
    }

    if (!buffer_.initialize())
    {
        return false;
    }

    initialized_ = true;

    return true;
}

HlsUpdateResult HlsBufferManager::update()
{
    HlsUpdateResult result{};

    if (!initialized_)
    {
        result.status =
            HlsUpdateStatus::SegmentRejected;

        return result;
    }

    std::error_code error;

    if (!std::filesystem::exists(
            playlistPath_,
            error
        ) || error)
    {
        result.status =
            HlsUpdateStatus::PlaylistUnavailable;

        return result;
    }

    HlsPlaylist playlist{};

    if (!HlsPlaylistParser::parseFile(
            playlistPath_,
            playlist
        ))
    {
        result.status =
            HlsUpdateStatus::InvalidPlaylist;

        return result;
    }

    result.endList = playlist.endList;

    for (const auto& entry : playlist.segments)
    {
        // Skip previously registered segments.

        if (lastSequence_.has_value() &&
            entry.sequenceNumber <=
                lastSequence_.value())
        {
            continue;
        }

        // Detect missing segments.

        if (lastSequence_.has_value())
        {
            if (lastSequence_.value() ==
                    std::numeric_limits<
                        std::uint64_t
                    >::max() ||
                entry.sequenceNumber !=
                    lastSequence_.value() + 1)
            {
                result.status =
                    HlsUpdateStatus::SequenceGap;

                return result;
            }
        }

        // Protect the media timeline from overflow.

        const auto maxPosition =
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::int64_t
                >::max()
            );

        if (entry.durationMs >
            maxPosition - nextPositionMs_)
        {
            result.status =
                HlsUpdateStatus::TimelineOverflow;

            return result;
        }

        SegmentMetadata segment{};

        segment.sequenceNumber =
            entry.sequenceNumber;

        segment.filePath =
            config_.storageDirectory /
            entry.fileName;

        segment.startTimeMs =
            nextPositionMs_;

        segment.durationMs =
            entry.durationMs;

        segment.discontinuity =
            entry.discontinuity;

        // CircularDiskBuffer validates the file
        // and obtains its actual size.

        if (!buffer_.addSegment(segment))
        {
            result.status =
                HlsUpdateStatus::SegmentRejected;

            return result;
        }

        lastSequence_ =
            segment.sequenceNumber;

        nextPositionMs_ =
            segment.endTimeMs();

        ++result.addedSegments;
    }

    result.status =
        result.addedSegments > 0
            ? HlsUpdateStatus::Updated
            : HlsUpdateStatus::NoChanges;

    return result;
}

bool HlsBufferManager::reset()
{
    if (!initialized_)
    {
        return false;
    }

    if (!buffer_.reset())
    {
        return false;
    }

    lastSequence_.reset();

    nextPositionMs_ = 0;

    return true;
}

const CircularDiskBuffer&
HlsBufferManager::buffer() const
{
    return buffer_;
}

std::optional<std::uint64_t>
HlsBufferManager::lastRegisteredSequence() const
{
    return lastSequence_;
}

std::uint64_t
HlsBufferManager::nextMediaPositionMs() const
{
    return nextPositionMs_;
}

void HlsBufferManager::markPlaylistPublished()
{
    buffer_.markPlaylistPublished();
}

bool HlsBufferManager::collectExpiredSegments()
{
    return buffer_.collectExpiredSegments();
}
