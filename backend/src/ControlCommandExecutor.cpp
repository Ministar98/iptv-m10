#include "ControlCommandExecutor.hpp"

#include <algorithm>
#include <exception>

ControlCommandExecutor::ControlCommandExecutor(
    SessionController& controller
)
    : controller_(controller)
{
}

bool ControlCommandExecutor::isChannelCommand(
    ControlCommandType type
) noexcept
{
    switch (type)
    {
        case ControlCommandType::SelectService:
        case ControlCommandType::SelectChannel:
        case ControlCommandType::NextChannel:
        case ControlCommandType::PreviousChannel:
            return true;

        default:
            return false;
    }
}

bool ControlCommandExecutor::isKnownCommand(
    ControlCommandType type
) noexcept
{
    switch (type)
    {
        case ControlCommandType::SelectService:
        case ControlCommandType::SelectChannel:
        case ControlCommandType::NextChannel:
        case ControlCommandType::PreviousChannel:
        case ControlCommandType::Pause:
        case ControlCommandType::Resume:
        case ControlCommandType::Seek:
        case ControlCommandType::GoLive:
        case ControlCommandType::GoOldest:
        case ControlCommandType::AcknowledgeSeek:
        case ControlCommandType::ReportPlaybackPosition:
            return true;
    }

    return false;
}

bool ControlCommandExecutor::dispatch(
    const ControlCommand& command
)
{
    switch (command.type)
    {
        case ControlCommandType::SelectService:
            return controller_.selectService(
                command.serviceId
            );

        case ControlCommandType::SelectChannel:
            return controller_.selectChannel(
                command.channelNumber
            );

        case ControlCommandType::NextChannel:
            return controller_.nextChannel();

        case ControlCommandType::PreviousChannel:
            return controller_.previousChannel();

        case ControlCommandType::Pause:
            return controller_.pause(
                command.generationId
            );

        case ControlCommandType::Resume:
            return controller_.resume(
                command.generationId
            );

        case ControlCommandType::Seek:
            return controller_.seek(
                command.generationId,
                command.seekPositionMs
            );

        case ControlCommandType::GoLive:
            return controller_.goLive(
                command.generationId
            );

        case ControlCommandType::GoOldest:
            return controller_.goOldest(
                command.generationId
            );

        case ControlCommandType::AcknowledgeSeek:
            return controller_.acknowledgeSeek(
                command.generationId,
                command.seekRevision,
                command.playbackPositionMs
            );

        case ControlCommandType::ReportPlaybackPosition:
            return controller_.reportPlaybackPosition(
                command.generationId,
                command.seekRevision,
                command.playbackPositionMs
            );
    }

    return false;
}

ControlOperationResult ControlCommandExecutor::execute(
    const ControlCommand& command
)
{
    ControlOperationResult result;

    result.command = command;
    result.session = controller_.snapshot();

    const auto reject =
        [&result](
            const std::string& code,
            const std::string& message
        )
        {
            result.status =
                ControlOperationStatus::Rejected;

            result.errorCode = code;
            result.errorMessage = message;
        };

    try
    {
        if (command.operationId == 0)
        {
            reject(
                "INVALID_OPERATION_ID",
                "Operation ID must be positive"
            );

            return result;
        }

        if (!isKnownCommand(command.type))
        {
            reject(
                "UNKNOWN_COMMAND",
                "Unsupported control command"
            );

            return result;
        }

        const bool channelCommand =
            isChannelCommand(command.type);

        const auto currentGeneration =
            result.session.generationId;

        if (command.generationId > currentGeneration)
        {
            reject(
                "STALE_GENERATION",
                "Command references a future generation"
            );

            return result;
        }

        if (!channelCommand &&
            command.generationId != currentGeneration)
        {
            reject(
                "STALE_GENERATION",
                "Command belongs to an outdated session"
            );

            return result;
        }

        if (command.type ==
            ControlCommandType::SelectService)
        {
            const auto services =
                controller_.services();

            const auto found = std::find_if(
                services.begin(),
                services.end(),
                [&command](const TvService& service)
                {
                    return service.serviceId ==
                        command.serviceId;
                }
            );

            if (found == services.end())
            {
                reject(
                    "SERVICE_NOT_FOUND",
                    "Requested service does not exist"
                );

                return result;
            }
        }

        if (command.type ==
            ControlCommandType::SelectChannel)
        {
            const auto services =
                controller_.services();

            const auto found = std::find_if(
                services.begin(),
                services.end(),
                [&command](const TvService& service)
                {
                    return service.channelNumber ==
                        command.channelNumber;
                }
            );

            if (found == services.end())
            {
                reject(
                    "CHANNEL_NOT_FOUND",
                    "Requested channel does not exist"
                );

                return result;
            }
        }

        if (!channelCommand &&
            result.session.state != SessionState::Running)
        {
            reject(
                "SESSION_NOT_RUNNING",
                "No running playback session"
            );

            return result;
        }

        const bool completed = dispatch(command);

        result.session = controller_.snapshot();

        if (completed)
        {
            result.status =
                ControlOperationStatus::Completed;

            return result;
        }

        if (result.session.state ==
            SessionState::Error)
        {
            result.status =
                ControlOperationStatus::Failed;

            result.errorCode = "SESSION_ERROR";

            result.errorMessage =
                result.session.errorMessage.empty()
                    ? "Session operation failed"
                    : result.session.errorMessage;

            return result;
        }

        if (!channelCommand &&
            command.generationId !=
                result.session.generationId)
        {
            reject(
                "STALE_GENERATION",
                "Session generation changed"
            );

            return result;
        }

        reject(
            channelCommand
                ? "INVALID_CHANNEL_STATE"
                : "INVALID_PLAYBACK_STATE",
            "Command cannot be applied in current state"
        );
    }
    catch (const std::exception& error)
    {
        result.status =
            ControlOperationStatus::Failed;

        result.errorCode = "INTERNAL_ERROR";
        result.errorMessage = error.what();

        result.session = controller_.snapshot();
    }
    catch (...)
    {
        result.status =
            ControlOperationStatus::Failed;

        result.errorCode = "INTERNAL_ERROR";
        result.errorMessage = "Unknown operation error";

        result.session = controller_.snapshot();
    }

    return result;
}
