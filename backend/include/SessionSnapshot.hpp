#pragma once

#include "TvService.hpp"
#include "PlaybackSnapshot.hpp"

#include <cstdint>
#include <optional>
#include <string>

enum class SessionState
{
    Idle,
    Starting,
    Running,
    Switching,
    Stopping,
    Error
};

inline constexpr const char*
sessionStateToString(SessionState state) noexcept
{
    switch (state)
    {
        case SessionState::Idle:
            return "IDLE";

        case SessionState::Starting:
            return "STARTING";

        case SessionState::Running:
            return "RUNNING";

        case SessionState::Switching:
            return "SWITCHING";

        case SessionState::Stopping:
            return "STOPPING";

        case SessionState::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

struct SessionSnapshot
{
    SessionState state = SessionState::Idle;

    std::uint64_t generationId = 0;

    std::optional<TvService> activeService;

    PlaybackSnapshot playback;

    std::string errorMessage;
};
