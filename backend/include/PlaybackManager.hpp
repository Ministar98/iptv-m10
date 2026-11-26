#pragma once

#include "PlaybackSnapshot.hpp"
#include "CircularDiskBuffer.hpp"

#include <cstdint>
#include <mutex>
#include <string>

class PlaybackManager
{
public:
    explicit PlaybackManager(
        std::uint64_t liveToleranceMs = 2000
    );

    PlaybackManager(const PlaybackManager&) = delete;

    PlaybackManager& operator=(
        const PlaybackManager&
    ) = delete;

    void reset(std::uint64_t generationId);

    bool updateBufferInterval(
        std::uint64_t generationId,
        const BufferInterval& interval
    );

    bool start();

    void stop();

    bool pause();

    bool resume();

    bool seek(std::int64_t positionMs);

    bool goLive();

    bool goOldest();

    bool acknowledgeSeek(
        std::uint64_t generationId,
        std::uint64_t seekRevision,
        std::uint64_t actualPositionMs
    );

    bool reportPlaybackPosition(
        std::uint64_t generationId,
        std::uint64_t seekRevision,
        std::uint64_t positionMs
    );

    void setError(const std::string& message);

    PlaybackSnapshot snapshot() const;

private:
    const std::uint64_t liveToleranceMs_;

    mutable std::mutex mutex_;

    PlaybackSnapshot state_{};

    bool canSeekLocked() const;

    bool seekLocked(std::uint64_t positionMs);

    void updateMetricsLocked();

    void updatePlayingStateLocked();

    void requestSeekLocked(
        std::uint64_t positionMs
    );

    std::uint64_t clampPositionLocked(
        std::uint64_t positionMs
    ) const;
};
