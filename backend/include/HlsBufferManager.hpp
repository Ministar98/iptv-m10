#pragma once

#include "CircularDiskBuffer.hpp"
#include "HlsPlaylistParser.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

enum class HlsUpdateStatus
{
    Updated,
    NoChanges,
    PlaylistUnavailable,
    InvalidPlaylist,
    SequenceGap,
    SegmentRejected,
    TimelineOverflow
};

struct HlsUpdateResult
{
    HlsUpdateStatus status =
        HlsUpdateStatus::NoChanges;

    std::size_t addedSegments = 0;

    bool endList = false;
};

class HlsBufferManager
{
public:
    explicit HlsBufferManager(
        const BufferConfig& config
    );

    HlsBufferManager(
        const HlsBufferManager&
    ) = delete;

    HlsBufferManager& operator=(
        const HlsBufferManager&
    ) = delete;

    bool initialize();

    HlsUpdateResult update();

    bool reset();

    void markPlaylistPublished();
    bool collectExpiredSegments();

    const CircularDiskBuffer& buffer() const;

    std::optional<std::uint64_t>
    lastRegisteredSequence() const;

    std::uint64_t nextMediaPositionMs() const;

private:
    BufferConfig config_;

    CircularDiskBuffer buffer_;

    std::filesystem::path playlistPath_;

    std::optional<std::uint64_t>
        lastSequence_;

    std::uint64_t nextPositionMs_ = 0;

    bool initialized_ = false;
};
