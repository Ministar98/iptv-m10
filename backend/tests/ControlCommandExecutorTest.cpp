#include "ControlCommandExecutor.hpp"
#include "ControlCommandQueue.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void require(
    bool condition,
    const std::string& message
)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void pass(const std::string& message)
{
    std::cout << "[PASS] " << message << '\n';
}

ControlCommand makeCommand(
    ControlCommandType type
)
{
    ControlCommand command;

    command.type = type;
    command.requestId = "test-request";
    command.operationId = 1;
    command.generationId = 0;

    return command;
}

void requireRejected(
    const ControlOperationResult& result,
    const std::string& expectedCode
)
{
    require(
        !result.success() &&
        result.status ==
            ControlOperationStatus::Rejected &&
        result.errorCode == expectedCode &&
        !result.errorMessage.empty(),
        "Expected rejection: " + expectedCode
    );
}

void testExecutor()
{
    const ServiceManager services;
    const HlsConfig hlsConfig;
    const BufferConfig bufferConfig;

    SessionController controller(
        services,
        hlsConfig,
        bufferConfig
    );

    ControlCommandExecutor executor(controller);

    auto command =
        makeCommand(ControlCommandType::Pause);

    command.operationId = 0;

    requireRejected(
        executor.execute(command),
        "INVALID_OPERATION_ID"
    );

    pass("Invalid operation ID rejected");

    command = makeCommand(
        static_cast<ControlCommandType>(999)
    );

    requireRejected(
        executor.execute(command),
        "UNKNOWN_COMMAND"
    );

    pass("Unknown command rejected");

    command = makeCommand(
        ControlCommandType::Pause
    );

    command.generationId = 99;

    requireRejected(
        executor.execute(command),
        "STALE_GENERATION"
    );

    pass("Stale playback generation rejected");

    command = makeCommand(
        ControlCommandType::SelectService
    );

    command.generationId = 99;
    command.serviceId = 2;

    requireRejected(
        executor.execute(command),
        "STALE_GENERATION"
    );

    pass("Stale channel command rejected");

    command = makeCommand(
        ControlCommandType::SelectService
    );

    command.serviceId = 999;

    requireRejected(
        executor.execute(command),
        "SERVICE_NOT_FOUND"
    );

    pass("Unknown service rejected");

    command = makeCommand(
        ControlCommandType::SelectChannel
    );

    command.channelNumber = 999;

    requireRejected(
        executor.execute(command),
        "CHANNEL_NOT_FOUND"
    );

    pass("Unknown channel rejected");

    for (const auto type : {
        ControlCommandType::Pause,
        ControlCommandType::Resume,
        ControlCommandType::Seek,
        ControlCommandType::GoLive,
        ControlCommandType::GoOldest,
        ControlCommandType::AcknowledgeSeek,
        ControlCommandType::ReportPlaybackPosition
    })
    {
        requireRejected(
            executor.execute(makeCommand(type)),
            "SESSION_NOT_RUNNING"
        );
    }

    pass("Idle session rejects all playback commands");

    ControlCommandQueue queue;

    command = makeCommand(
        ControlCommandType::Pause
    );

    command.requestId = "request-123";

    const auto submitted = queue.submit(command);

    require(
        submitted.accepted(),
        "Queue submission failed"
    );

    const auto queued = queue.tryPop();

    require(
        queued.has_value(),
        "Missing queued command"
    );

    const auto result = executor.execute(*queued);

    require(
        result.command.operationId ==
            submitted.operationId &&
        result.command.requestId == "request-123" &&
        result.command.type ==
            ControlCommandType::Pause,
        "Operation correlation failed"
    );

    pass("Queue and executor operation correlation");

    require(
        result.session.state == SessionState::Idle &&
        result.session.generationId == 0 &&
        !result.session.activeService.has_value(),
        "Invalid operation session snapshot"
    );

    pass("Operation result contains session snapshot");

    require(
        controller.snapshot().state ==
            SessionState::Idle,
        "Rejected commands changed session state"
    );

    pass("Rejected commands preserve session state");
}

} // namespace

int main()
{
    try
    {
        testExecutor();

        std::cout
            << "\nAll Control Command Executor tests passed.\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "[FAIL] "
            << error.what()
            << '\n';

        return 1;
    }
}
