#include "ControlWorker.hpp"
#include "EventQueue.hpp"
#include "ApiOperationJson.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace
{

using namespace std::chrono_literals;

void require(bool condition, const std::string& message)
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
        ("control_pipeline_test_" +
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

        const auto snapshot = controller.snapshot();

        if (snapshot.playback.bufferAvailable &&
            snapshot.playback.bufferDurationMs >= 4000)
        {
            return snapshot;
        }

        std::this_thread::sleep_for(100ms);
    }

    throw std::runtime_error("Buffer startup timeout");
}

ControlCommand makeCommand(
    ControlCommandType type,
    std::uint64_t generationId
)
{
    ControlCommand command;

    command.type = type;
    command.generationId = generationId;
    command.requestId = "pipeline-test";

    return command;
}

ApiJson::Json submitAndWait(
    ControlCommandQueue& commands,
    EventQueue& events,
    const ControlCommand& command
)
{
    const auto submission = commands.submit(command);

    require(
        submission.accepted(),
        "Command submission failed"
    );

    const auto deadline =
        std::chrono::steady_clock::now() + 15s;

    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto event = events.tryPop();

        if (event)
        {
            require(
                (*event)["type"] == "OPERATION_RESULT",
                "Unexpected event type"
            );

            require(
                (*event)["operationId"] ==
                    submission.operationId,
                "Operation ID mismatch"
            );

            require(
                (*event)["requestId"] ==
                    command.requestId,
                "Request ID mismatch"
            );

            return *event;
        }

        std::this_thread::sleep_for(20ms);
    }

    throw std::runtime_error(
        "Operation result timeout"
    );
}

void requireCompleted(const ApiJson::Json& event)
{
    require(
        event["payload"]["status"] == "COMPLETED",
        "Operation failed: " + event.dump()
    );
}

void requireRejected(
    const ApiJson::Json& event,
    const std::string& code
)
{
    require(
        event["payload"]["status"] == "REJECTED" &&
        event["payload"]["error"]["code"] == code,
        "Expected rejection: " + code +
        ", received: " + event.dump()
    );
}

void testPipeline()
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

    require(
        worker.start(),
        "Control worker startup failed"
    );

    auto snapshot = waitForBuffer(controller);

    const auto generation1 = snapshot.generationId;

    require(
        snapshot.activeService &&
        snapshot.activeService->serviceId == 1,
        "Invalid initial service"
    );

    pass("Control pipeline startup");

    pass("Initial RTP and HLS buffer available");

    auto event = submitAndWait(
        commands,
        events,
        makeCommand(
            ControlCommandType::GoLive,
            generation1
        )
    );

    requireCompleted(event);

    pass("Asynchronous Go Live");

    snapshot = controller.snapshot();

    auto command = makeCommand(
        ControlCommandType::AcknowledgeSeek,
        generation1
    );

    command.seekRevision =
        snapshot.playback.seekRevision;

    command.playbackPositionMs =
        snapshot.playback.playbackPositionMs;

    requireCompleted(
        submitAndWait(commands, events, command)
    );

    pass("Asynchronous seek acknowledgement");

    event = submitAndWait(
        commands,
        events,
        makeCommand(
            ControlCommandType::Pause,
            generation1
        )
    );

    requireCompleted(event);

    require(
        controller.snapshot().playback.state ==
            PlaybackState::Paused,
        "Playback was not paused"
    );

    pass("Asynchronous Pause");

    const auto paused =
        controller.snapshot().playback;

    const auto growthDeadline =
        std::chrono::steady_clock::now() + 10s;

    bool bufferGrew = false;

    while (std::chrono::steady_clock::now() <
           growthDeadline)
    {
        require(
            controller.update(),
            "Paused session update failed"
        );

        const auto current =
            controller.snapshot().playback;

        require(
            current.state == PlaybackState::Paused &&
            current.playbackPositionMs ==
                paused.playbackPositionMs,
            "Paused playback position changed"
        );

        if (current.livePositionMs >
            paused.livePositionMs)
        {
            bufferGrew = true;

            break;
        }

        std::this_thread::sleep_for(100ms);
    }

    require(
        bufferGrew,
        "Buffer stopped growing during pause"
    );

    pass("RTP ingestion continues during Pause");

    requireCompleted(
        submitAndWait(
            commands,
            events,
            makeCommand(
                ControlCommandType::Resume,
                generation1
            )
        )
    );

    pass("Asynchronous Resume");

    requireCompleted(
        submitAndWait(
            commands,
            events,
            makeCommand(
                ControlCommandType::GoOldest,
                generation1
            )
        )
    );

    snapshot = controller.snapshot();

    require(
        snapshot.playback.playbackPositionMs ==
            snapshot.playback.oldestPositionMs,
        "Go Oldest position mismatch"
    );

    pass("Asynchronous Go Oldest");

    command = makeCommand(
        ControlCommandType::Seek,
        generation1
    );

    command.seekPositionMs =
        static_cast<std::int64_t>(
            snapshot.playback.oldestPositionMs + 1000
        );

    requireCompleted(
        submitAndWait(commands, events, command)
    );

    require(
        controller.snapshot().playback.playbackPositionMs ==
            static_cast<std::uint64_t>(
                command.seekPositionMs
            ),
        "Seek position mismatch"
    );

    pass("Asynchronous absolute timeline seek");

    command = makeCommand(
        ControlCommandType::SelectService,
        generation1
    );

    command.serviceId = 2;

    event = submitAndWait(
        commands,
        events,
        command
    );

    requireCompleted(event);

    snapshot = controller.snapshot();

    const auto generation2 = snapshot.generationId;

    require(
        generation2 == generation1 + 1 &&
        snapshot.activeService &&
        snapshot.activeService->serviceId == 2,
        "Channel switch failed"
    );

    pass("Asynchronous channel switch");

    require(
        event["generationId"] == generation2 &&
        event["payload"]["requestedGenerationId"] ==
            generation1,
        "Channel switch event generation mismatch"
    );

    pass("Channel switch JSON generation correlation");

    requireRejected(
        submitAndWait(
            commands,
            events,
            makeCommand(
                ControlCommandType::Pause,
                generation1
            )
        ),
        "STALE_GENERATION"
    );

    pass("Stale generation rejected asynchronously");

    command = makeCommand(
        ControlCommandType::SelectService,
        generation2
    );

    command.serviceId = 999;

    requireRejected(
        submitAndWait(commands, events, command),
        "SERVICE_NOT_FOUND"
    );

    pass("Unknown service rejected asynchronously");

    snapshot = waitForBuffer(controller);

    require(
        snapshot.generationId == generation2,
        "Unexpected generation change"
    );

    requireCompleted(
        submitAndWait(
            commands,
            events,
            makeCommand(
                ControlCommandType::Pause,
                generation2
            )
        )
    );

    pass("New generation accepts playback commands");

    worker.stop();

    events.close();

    const auto workerStats = worker.statistics();
    const auto eventStats = events.statistics();

    std::cout
        << "\nProcessed: " << workerStats.processed
        << "\nCompleted: " << workerStats.completed
        << "\nRejected: " << workerStats.rejected
        << "\nFailed: " << workerStats.failed
        << "\nCallback errors: " << workerStats.callbackErrors
        << "\nPublished events: " << eventStats.published
        << "\nConsumed events: " << eventStats.consumed
        << "\nDropped events: " << eventStats.dropped
        << "\nQueued events: " << eventStats.queued
        << '\n';

    require(
        workerStats.processed == 10 &&
        workerStats.completed == 8 &&
        workerStats.rejected == 2 &&
        workerStats.failed == 0 &&
        workerStats.callbackErrors == 0,
        "Unexpected control worker statistics"
    );

    pass("Control worker statistics");

    require(
        eventStats.published == 10 &&
        eventStats.consumed == 10 &&
        eventStats.dropped == 0 &&
        eventStats.queued == 0,
        "Operation events were lost"
    );

    pass("All operation events delivered");

    controller.stop();

    require(
        !worker.isRunning() &&
        commands.isClosed() &&
        events.statistics().closed &&
        controller.snapshot().state ==
            SessionState::Idle,
        "Pipeline shutdown failed"
    );

    pass("Clean control pipeline shutdown");
}

} // namespace

int main()
{
    try
    {
        testPipeline();

        std::cout
            << "\nAll Control Pipeline tests passed.\n";

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
