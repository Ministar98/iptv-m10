#include "ControlWorker.hpp"
#include "EventQueue.hpp"
#include "ApiOperationJson.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

namespace
{

using namespace std::chrono_literals;

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

class RuntimeDirectory
{
public:
    const std::filesystem::path path =
        std::filesystem::path("runtime") /
        ("rapid_channel_test_" +
         std::to_string(getpid()));

    ~RuntimeDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

SessionSnapshot waitForBuffer(
    SessionController& controller
)
{
    const auto deadline =
        std::chrono::steady_clock::now() + 20s;

    while (std::chrono::steady_clock::now() < deadline)
    {
        require(
            controller.update(),
            "Session update failed"
        );

        const auto snapshot =
            controller.snapshot();

        if (snapshot.playback.bufferAvailable &&
            snapshot.playback.bufferDurationMs >= 4000)
        {
            return snapshot;
        }

        std::this_thread::sleep_for(100ms);
    }

    throw std::runtime_error(
        "Buffer startup timeout"
    );
}

ControlCommand makeCommand(
    ControlCommandType type,
    std::uint64_t generationId,
    const std::string& requestId
)
{
    ControlCommand command;

    command.type = type;
    command.generationId = generationId;
    command.requestId = requestId;

    return command;
}

std::uint64_t submit(
    ControlCommandQueue& queue,
    const ControlCommand& command
)
{
    const auto result = queue.submit(command);

    require(
        result.accepted(),
        "Command submission failed"
    );

    return result.operationId;
}

ApiJson::Json waitForEvent(
    EventQueue& events,
    std::uint64_t operationId
)
{
    const auto deadline =
        std::chrono::steady_clock::now() + 20s;

    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto event = events.tryPop();

        if (event)
        {
            require(
                (*event)["type"] ==
                    "OPERATION_RESULT",
                "Unexpected event type"
            );

            require(
                (*event)["operationId"] ==
                    operationId,
                "Operation ordering mismatch"
            );

            return *event;
        }

        std::this_thread::sleep_for(20ms);
    }

    throw std::runtime_error(
        "Operation event timeout"
    );
}

void requireCompleted(
    const ApiJson::Json& event
)
{
    require(
        event["payload"]["status"] ==
            "COMPLETED",
        "Expected completed operation: " +
            event.dump()
    );
}

void requireRejected(
    const ApiJson::Json& event,
    const std::string& code
)
{
    require(
        event["payload"]["status"] ==
            "REJECTED" &&
        event["payload"]["error"]["code"] ==
            code,
        "Expected rejection: " + code +
            ", received: " + event.dump()
    );
}

void testRapidChannelSwitch()
{
    RuntimeDirectory runtime;

    HlsConfig hlsConfig;

    hlsConfig.outputDirectory = runtime.path;
    hlsConfig.segmentDurationSeconds = 2;
    hlsConfig.playlistSize = 60;
    hlsConfig.playlistFilename = "producer.m3u8";
    hlsConfig.deleteSegments = false;

    BufferConfig bufferConfig;

    bufferConfig.storageDirectory = runtime.path;

    bufferConfig.deletionMode =
        SegmentDeletionMode::Deferred;

    const ServiceManager services;

    SessionController controller(
        services,
        hlsConfig,
        bufferConfig
    );

    ControlCommandQueue commands(64);
    EventQueue events(128);

    ControlCommandExecutor executor(controller);

    ControlWorker worker(
        commands,
        executor,
        [&](const ControlOperationResult& result)
        {
            if (!events.publish(
                    ApiJson::operationResult(result)))
            {
                throw std::runtime_error(
                    "Event publication failed"
                );
            }
        }
    );

    require(
        controller.start(),
        "Session startup failed"
    );

    auto snapshot = waitForBuffer(controller);

    const auto generation1 =
        snapshot.generationId;

    require(
        snapshot.activeService &&
        snapshot.activeService->serviceId == 1,
        "Invalid initial service"
    );

    pass("Initial channel available");

    auto command = makeCommand(
        ControlCommandType::SelectService,
        generation1,
        "select-2"
    );

    command.serviceId = 2;

    const auto operation1 =
        submit(commands, command);

    command = makeCommand(
        ControlCommandType::SelectService,
        generation1,
        "select-3"
    );

    command.serviceId = 3;

    const auto operation2 =
        submit(commands, command);

    command = makeCommand(
        ControlCommandType::Pause,
        generation1,
        "stale-pause"
    );

    const auto operation3 =
        submit(commands, command);

    require(
        operation1 < operation2 &&
        operation2 < operation3,
        "Invalid operation ordering"
    );

    pass("Rapid commands queued in FIFO order");

    require(
        worker.start(),
        "Control worker startup failed"
    );

    const auto event1 =
        waitForEvent(events, operation1);

    requireCompleted(event1);

    require(
        event1["generationId"] ==
            generation1 + 1,
        "First channel generation mismatch"
    );

    pass("First channel selection completed");

    const auto event2 =
        waitForEvent(events, operation2);

    requireCompleted(event2);

    require(
        event2["generationId"] ==
            generation1 + 2 &&
        event2["payload"]["requestedGenerationId"] ==
            generation1,
        "Second channel generation mismatch"
    );

    pass("Queued channel selection survives generation change");

    requireRejected(
        waitForEvent(events, operation3),
        "STALE_GENERATION"
    );

    pass("Old playback command rejected");

    snapshot = waitForBuffer(controller);

    require(
        snapshot.activeService &&
        snapshot.activeService->serviceId == 3 &&
        snapshot.generationId == generation1 + 2,
        "Latest channel selection did not win"
    );

    pass("Latest channel selection wins");

    const auto generation3 =
        snapshot.generationId;

    command = makeCommand(
        ControlCommandType::SelectService,
        generation3 + 100,
        "future-channel"
    );

    command.serviceId = 1;

    const auto operation4 =
        submit(commands, command);

    requireRejected(
        waitForEvent(events, operation4),
        "STALE_GENERATION"
    );

    pass("Future channel generation rejected");

    command = makeCommand(
        ControlCommandType::SelectService,
        generation3,
        "invalid-service"
    );

    command.serviceId = 999;

    const auto operation5 =
        submit(commands, command);

    requireRejected(
        waitForEvent(events, operation5),
        "SERVICE_NOT_FOUND"
    );

    pass("Invalid service cannot replace active channel");

    command = makeCommand(
        ControlCommandType::SelectService,
        generation3,
        "same-service"
    );

    command.serviceId = 3;

    const auto operation6 =
        submit(commands, command);

    requireCompleted(
        waitForEvent(events, operation6)
    );

    snapshot = controller.snapshot();

    require(
        snapshot.activeService &&
        snapshot.activeService->serviceId == 3 &&
        snapshot.generationId == generation3,
        "Same service selection restarted session"
    );

    pass("Same service selection preserves generation");

    command = makeCommand(
        ControlCommandType::NextChannel,
        generation3,
        "next-1"
    );

    const auto operation7 =
        submit(commands, command);

    command.requestId = "next-2";

    const auto operation8 =
        submit(commands, command);

    requireCompleted(
        waitForEvent(events, operation7)
    );

    requireCompleted(
        waitForEvent(events, operation8)
    );

    snapshot = waitForBuffer(controller);

    require(
        snapshot.activeService &&
        snapshot.activeService->serviceId == 2 &&
        snapshot.generationId == generation3 + 2,
        "Rapid next channel commands failed"
    );

    pass("Rapid relative channel navigation");

    command = makeCommand(
        ControlCommandType::Pause,
        snapshot.generationId,
        "current-pause"
    );

    const auto operation9 =
        submit(commands, command);

    requireCompleted(
        waitForEvent(events, operation9)
    );

    require(
        controller.snapshot().playback.state ==
            PlaybackState::Paused,
        "Current playback command failed"
    );

    pass("Current generation accepts playback commands");

    worker.stop();

    events.close();

    const auto workerStats =
        worker.statistics();

    const auto eventStats =
        events.statistics();

    std::cout
        << "\nProcessed: " << workerStats.processed
        << "\nCompleted: " << workerStats.completed
        << "\nRejected: " << workerStats.rejected
        << "\nFailed: " << workerStats.failed
        << "\nCallback errors: "
        << workerStats.callbackErrors
        << "\nPublished events: " << eventStats.published
        << "\nConsumed events: " << eventStats.consumed
        << "\nDropped events: " << eventStats.dropped
        << '\n';

    require(
        workerStats.processed == 9 &&
        workerStats.completed == 6 &&
        workerStats.rejected == 3 &&
        workerStats.failed == 0 &&
        workerStats.callbackErrors == 0,
        "Unexpected worker statistics"
    );

    pass("Rapid channel command statistics");

    require(
        eventStats.published == 9 &&
        eventStats.consumed == 9 &&
        eventStats.dropped == 0 &&
        eventStats.queued == 0,
        "Operation events were lost"
    );

    pass("All operation events delivered");

    controller.stop();

    require(
        controller.snapshot().state ==
            SessionState::Idle &&
        !worker.isRunning() &&
        commands.isClosed() &&
        events.statistics().closed,
        "Shutdown failed"
    );

    pass("Clean shutdown");
}

} // namespace

int main()
{
    try
    {
        testRapidChannelSwitch();

        std::cout
            << "\nAll Rapid Channel Switch tests passed.\n";

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
