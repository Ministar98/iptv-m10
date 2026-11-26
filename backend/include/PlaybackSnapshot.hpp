#pragma once

#include "PlaybackState.hpp"

#include <cstdint>
#include <string>

struct PlaybackSnapshot
{
    PlaybackState state =
        PlaybackState::Stopped;

    // Availability of published HLS content.

    bool bufferAvailable = false;

    // Absolute positions on the media timeline.

    std::uint64_t oldestPositionMs = 0;

    std::uint64_t livePositionMs = 0;

    std::uint64_t playbackPositionMs = 0;

    // Duration of the currently available interval.

    std::uint64_t bufferDurationMs = 0;

    // Distance between playback position and live edge.

    std::uint64_t delayFromLiveMs = 0;

    // Position relative to the oldest available content.

    std::uint64_t positionInBufferMs = 0;

    // True when playback is sufficiently close to live.

    bool atLiveEdge = false;

    // True when an old playback position has expired
    // and must be moved to available content.

    bool positionExpired = false;

    // True when the player must apply a new position.

    bool seekRequired = false;

    // Changes whenever a new seek is requested.

    std::uint64_t seekRevision = 0;

    // Identifies the current media timeline.

    std::uint64_t generationId = 0;

    // Last playback error, if any.

    std::string errorMessage;
};
