#include "PlaybackManager.hpp"

#include <algorithm>

PlaybackManager::PlaybackManager(
    std::uint64_t liveToleranceMs
)
    : liveToleranceMs_(liveToleranceMs)
{
}

void PlaybackManager::reset(
    std::uint64_t generationId
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    state_ = PlaybackSnapshot{};

    state_.generationId = generationId;
}

bool PlaybackManager::updateBufferInterval(
    std::uint64_t generationId,
    const BufferInterval& interval
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (generationId != state_.generationId)
    {
        return false;
    }

    if (
        interval.available &&
        (
            interval.endTimeMs <= interval.startTimeMs ||
            interval.durationMs !=
                interval.endTimeMs - interval.startTimeMs
        )
    )
    {
        return false;
    }

    if (!interval.available)
    {
        state_.bufferAvailable = false;

        state_.oldestPositionMs = 0;
        state_.livePositionMs = 0;
        state_.playbackPositionMs = 0;

        state_.seekRequired = false;
        state_.positionExpired = false;

        if (
            state_.state == PlaybackState::Live ||
            state_.state == PlaybackState::Timeshift
        )
        {
            state_.state = PlaybackState::Buffering;
        }

        updateMetricsLocked();

        return true;
    }

    if (
        state_.bufferAvailable &&
        (
            interval.startTimeMs < state_.oldestPositionMs ||
            interval.endTimeMs < state_.livePositionMs
        )
    )
    {
        return false;
    }

    const bool previouslyAvailable =
        state_.bufferAvailable;

    state_.bufferAvailable = true;

    state_.oldestPositionMs =
        interval.startTimeMs;

    state_.livePositionMs =
        interval.endTimeMs;

    if (
        state_.state == PlaybackState::Stopped ||
        state_.state == PlaybackState::Error
    )
    {
        state_.playbackPositionMs =
            state_.oldestPositionMs;

        updateMetricsLocked();

        return true;
    }

    if (state_.state == PlaybackState::Buffering)
    {
        state_.state = PlaybackState::Live;

        requestSeekLocked(state_.livePositionMs);

        updatePlayingStateLocked();

        return true;
    }

    if (!previouslyAvailable)
    {
        state_.positionExpired = true;

        requestSeekLocked(
            state_.oldestPositionMs
        );
    }
    else if (
        state_.playbackPositionMs <
        state_.oldestPositionMs
    )
    {
        state_.positionExpired = true;

        requestSeekLocked(
            state_.oldestPositionMs
        );
    }
    else if (
        state_.playbackPositionMs >
        state_.livePositionMs
    )
    {
        requestSeekLocked(
            state_.livePositionMs
        );
    }

    updatePlayingStateLocked();

    return true;
}

bool PlaybackManager::start()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_.state != PlaybackState::Stopped)
    {
        return false;
    }

    state_.errorMessage.clear();
    state_.positionExpired = false;

    if (!state_.bufferAvailable)
    {
        state_.state = PlaybackState::Buffering;

        updateMetricsLocked();

        return true;
    }

    state_.state = PlaybackState::Live;

    requestSeekLocked(state_.livePositionMs);

    updateMetricsLocked();

    return true;
}

void PlaybackManager::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    state_.state = PlaybackState::Stopped;

    state_.playbackPositionMs =
        state_.oldestPositionMs;

    state_.positionExpired = false;
    state_.seekRequired = false;
    state_.errorMessage.clear();

    updateMetricsLocked();
}

bool PlaybackManager::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (
        state_.state != PlaybackState::Live &&
        state_.state != PlaybackState::Timeshift
    )
    {
        return false;
    }

    state_.state = PlaybackState::Paused;

    updateMetricsLocked();

    return true;
}

bool PlaybackManager::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_.state != PlaybackState::Paused)
    {
        return false;
    }

    if (!state_.bufferAvailable)
    {
        state_.state = PlaybackState::Buffering;
        state_.seekRequired = false;

        updateMetricsLocked();

        return true;
    }

    if (
        state_.playbackPositionMs <
        state_.oldestPositionMs
    )
    {
        state_.positionExpired = true;

        requestSeekLocked(
            state_.oldestPositionMs
        );
    }
    else if (
        state_.playbackPositionMs >
        state_.livePositionMs
    )
    {
        requestSeekLocked(
            state_.livePositionMs
        );
    }

    state_.state = PlaybackState::Timeshift;

    updatePlayingStateLocked();

    return true;
}

bool PlaybackManager::canSeekLocked() const
{
    return state_.bufferAvailable &&
        (
            state_.state == PlaybackState::Live ||
            state_.state == PlaybackState::Timeshift ||
            state_.state == PlaybackState::Paused
        );
}

bool PlaybackManager::seekLocked(
    std::uint64_t positionMs
)
{
    if (!canSeekLocked())
    {
        return false;
    }

    state_.positionExpired = false;

    requestSeekLocked(positionMs);

    state_.state = PlaybackState::Timeshift;

    updatePlayingStateLocked();

    return true;
}

bool PlaybackManager::seek(
    std::int64_t positionMs
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const std::uint64_t target =
        positionMs < 0
            ? 0
            : static_cast<std::uint64_t>(positionMs);

    return seekLocked(target);
}

bool PlaybackManager::goLive()
{
    std::lock_guard<std::mutex> lock(mutex_);

    return seekLocked(state_.livePositionMs);
}

bool PlaybackManager::goOldest()
{
    std::lock_guard<std::mutex> lock(mutex_);

    return seekLocked(state_.oldestPositionMs);
}

bool PlaybackManager::acknowledgeSeek(
    std::uint64_t generationId,
    std::uint64_t seekRevision,
    std::uint64_t actualPositionMs
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (
        generationId != state_.generationId ||
        seekRevision != state_.seekRevision ||
        !state_.seekRequired ||
        !canSeekLocked()
    )
    {
        return false;
    }

    if (
        actualPositionMs < state_.oldestPositionMs ||
        actualPositionMs > state_.livePositionMs
    )
    {
        return false;
    }

    state_.playbackPositionMs =
        actualPositionMs;

    state_.seekRequired = false;
    state_.positionExpired = false;

    updatePlayingStateLocked();

    return true;
}

bool PlaybackManager::reportPlaybackPosition(
    std::uint64_t generationId,
    std::uint64_t seekRevision,
    std::uint64_t positionMs
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (
        generationId != state_.generationId ||
        seekRevision != state_.seekRevision
    )
    {
        return false;
    }

    if (!state_.bufferAvailable)
    {
        return false;
    }

    if (
        state_.state != PlaybackState::Live &&
        state_.state != PlaybackState::Timeshift
    )
    {
        return false;
    }

    if (state_.seekRequired)
    {
        return false;
    }

    if (
        positionMs < state_.oldestPositionMs ||
        positionMs > state_.livePositionMs
    )
    {
        return false;
    }

    state_.playbackPositionMs = positionMs;

    updatePlayingStateLocked();

    return true;
}

void PlaybackManager::setError(
    const std::string& message
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    state_.state = PlaybackState::Error;
    state_.errorMessage = message;
    state_.seekRequired = false;
    state_.positionExpired = false;

    updateMetricsLocked();
}

PlaybackSnapshot PlaybackManager::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return state_;
}

std::uint64_t PlaybackManager::clampPositionLocked(
    std::uint64_t positionMs
) const
{
    if (!state_.bufferAvailable)
    {
        return 0;
    }

    return std::clamp(
        positionMs,
        state_.oldestPositionMs,
        state_.livePositionMs
    );
}

void PlaybackManager::requestSeekLocked(
    std::uint64_t positionMs
)
{
    if (!state_.bufferAvailable)
    {
        return;
    }

    state_.playbackPositionMs =
        clampPositionLocked(positionMs);

    state_.seekRequired = true;

    ++state_.seekRevision;
}

void PlaybackManager::updateMetricsLocked()
{
    if (!state_.bufferAvailable)
    {
        state_.bufferDurationMs = 0;
        state_.delayFromLiveMs = 0;
        state_.positionInBufferMs = 0;
        state_.atLiveEdge = false;

        return;
    }

    state_.bufferDurationMs =
        state_.livePositionMs -
        state_.oldestPositionMs;

    if (
        state_.state == PlaybackState::Stopped ||
        state_.state == PlaybackState::Error ||
        state_.state == PlaybackState::Buffering
    )
    {
        state_.delayFromLiveMs = 0;
        state_.positionInBufferMs = 0;
        state_.atLiveEdge = false;

        return;
    }

    state_.playbackPositionMs =
        clampPositionLocked(
            state_.playbackPositionMs
        );

    state_.delayFromLiveMs =
        state_.livePositionMs -
        state_.playbackPositionMs;

    state_.positionInBufferMs =
        state_.playbackPositionMs -
        state_.oldestPositionMs;

    state_.atLiveEdge =
        state_.delayFromLiveMs <= liveToleranceMs_;
}

void PlaybackManager::updatePlayingStateLocked()
{
    updateMetricsLocked();

    if (
        state_.state != PlaybackState::Live &&
        state_.state != PlaybackState::Timeshift
    )
    {
        return;
    }

    if (!state_.bufferAvailable)
    {
        state_.state = PlaybackState::Buffering;

        updateMetricsLocked();

        return;
    }

    state_.state =
        state_.atLiveEdge
            ? PlaybackState::Live
            : PlaybackState::Timeshift;
}
