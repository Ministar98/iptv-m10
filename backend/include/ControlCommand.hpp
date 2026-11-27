#pragma once

#include <cstdint>
#include <string>

enum class ControlCommandType
{
    SelectService,
    SelectChannel,
    NextChannel,
    PreviousChannel,
    Pause,
    Resume,
    Seek,
    GoLive,
    GoOldest,
    AcknowledgeSeek,
    ReportPlaybackPosition
};

inline constexpr const char*
controlCommandTypeToString(
    ControlCommandType type
) noexcept
{
    switch (type)
    {
        case ControlCommandType::SelectService:
            return "SELECT_SERVICE";

        case ControlCommandType::SelectChannel:
            return "SELECT_CHANNEL";

        case ControlCommandType::NextChannel:
            return "NEXT_CHANNEL";

        case ControlCommandType::PreviousChannel:
            return "PREVIOUS_CHANNEL";

        case ControlCommandType::Pause:
            return "PAUSE";

        case ControlCommandType::Resume:
            return "RESUME";

        case ControlCommandType::Seek:
            return "SEEK";

        case ControlCommandType::GoLive:
            return "GO_LIVE";

        case ControlCommandType::GoOldest:
            return "GO_OLDEST";

        case ControlCommandType::AcknowledgeSeek:
            return "ACKNOWLEDGE_SEEK";

        case ControlCommandType::ReportPlaybackPosition:
            return "REPORT_PLAYBACK_POSITION";
    }

    return "UNKNOWN";
}

struct ControlCommand
{
    ControlCommandType type =
        ControlCommandType::GoLive;

    std::string requestId;

    std::uint64_t operationId = 0;

    std::uint64_t generationId = 0;

    std::uint16_t serviceId = 0;

    unsigned int channelNumber = 0;

    std::int64_t seekPositionMs = 0;

    std::uint64_t seekRevision = 0;

    std::uint64_t playbackPositionMs = 0;
};
