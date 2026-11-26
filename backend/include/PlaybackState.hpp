#pragma once

enum class PlaybackState
{
    Stopped,
    Buffering,
    Live,
    Timeshift,
    Paused,
    Error
};

inline constexpr const char* playbackStateToString(
    PlaybackState state
) noexcept
{
    switch (state)
    {
        case PlaybackState::Stopped:
            return "STOPPED";

        case PlaybackState::Buffering:
            return "BUFFERING";

        case PlaybackState::Live:
            return "LIVE";

        case PlaybackState::Timeshift:
            return "TIMESHIFT";

        case PlaybackState::Paused:
            return "PAUSED";

        case PlaybackState::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}
