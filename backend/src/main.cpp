#include "SessionController.hpp"
#include "OttMetadataManager.hpp"
#include "ControlCommandQueue.hpp"
#include "ControlCommandExecutor.hpp"
#include "ControlWorker.hpp"
#include "EventQueue.hpp"
#include "ApiOperationJson.hpp"
#include "ApiStateCache.hpp"
#include "ApiRequestHandler.hpp"
#include "HttpServer.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <filesystem>

#include <poll.h>
#include <unistd.h>

namespace
{

volatile std::sig_atomic_t stopRequested = 0;

constexpr auto CONTROL_INTERVAL =
    std::chrono::milliseconds(100);

constexpr auto STATISTICS_INTERVAL =
    std::chrono::seconds(5);

void signalHandler(int)
{
    stopRequested = 1;
}

void printCommands()
{
    std::cout
        << "\n========== IPTV M10 ==========\n"
        << "n          - Next channel\n"
        << "p          - Previous channel\n"
        << "NUM        - Select channel\n"
        << "pause      - Pause playback\n"
        << "resume     - Resume playback\n"
        << "seek MS    - Seek to absolute position\n"
        << "live       - Go live\n"
        << "oldest     - Go to oldest position\n"
        << "s          - Statistics\n"
        << "l          - Service list\n"
        << "h          - Help\n"
        << "q          - Quit\n"
        << "==============================\n";
}

void printServices(const ServiceManager& manager)
{
    std::cout << "\n--- TV Services ---\n";

    for (const auto& service : manager.services())
    {
        std::cout
            << service.channelNumber
            << " - "
            << service.name
            << " (Service ID: "
            << service.serviceId
            << ")\n";
    }
}

void printStatistics(
    const SessionController& controller,
    const ControlCommandQueue& commands,
    const ControlWorker& worker,
    const EventQueue& events
)
{
    const auto session = controller.snapshot();
    const auto statistics = controller.statistics();
    const auto workerStats = worker.statistics();
    const auto eventStats = events.statistics();

    const auto& playback = session.playback;

    std::cout
        << "\n========== IPTV Statistics ==========\n"
        << "Session state: "
        << sessionStateToString(session.state) << '\n'
        << "Generation ID: "
        << session.generationId << '\n';

    if (session.activeService)
    {
        std::cout
            << "Active service: "
            << session.activeService->name << '\n'
            << "Service ID: "
            << session.activeService->serviceId << '\n'
            << "Channel number: "
            << session.activeService->channelNumber << '\n';
    }

    if (statistics)
    {
        const auto& producer = statistics->producer;
        const auto& queue = statistics->queue;
        const auto& consumer = statistics->consumer;

        std::cout
            << "\n--- RTP Producer ---\n"
            << "Received datagrams: "
            << producer.receivedDatagrams << '\n'
            << "Valid RTP packets: "
            << producer.validRtpPackets << '\n'
            << "Invalid RTP packets: "
            << producer.invalidRtpPackets << '\n'
            << "Unexpected SSRC packets: "
            << producer.unexpectedSsrcPackets << '\n'
            << "RTP discontinuities: "
            << producer.rtpDiscontinuities << '\n'
            << "Valid TS packets: "
            << producer.validTsPackets << '\n'
            << "Invalid TS packets: "
            << producer.invalidTsPackets << '\n'
            << "Transport errors: "
            << producer.transportErrors << '\n'
            << "Valid PAT sections: "
            << producer.validPatSections << '\n'
            << "Invalid PAT sections: "
            << producer.invalidPatSections << '\n'
            << "Valid PMT sections: "
            << producer.validPmtSections << '\n'
            << "Invalid PMT sections: "
            << producer.invalidPmtSections << '\n'
            << "Unexpected PMT sections: "
            << producer.unexpectedPmtSections << '\n'
            << "Detected programs: "
            << producer.detectedPrograms << '\n'
            << "Resolved PMT tables: "
            << producer.resolvedPmtTables << '\n'
            << "Queued blocks: "
            << producer.queuedBlocks << '\n'
            << "Queued bytes: "
            << producer.queuedBytes << '\n'
            << "Dropped blocks: "
            << producer.droppedBlocks << '\n'
            << "Dropped bytes: "
            << producer.droppedBytes << '\n'
            << "Receive timeouts: "
            << producer.receiveTimeouts << '\n'
            << "Receive errors: "
            << producer.receiveErrors << '\n'
            << "Signal available: "
            << producer.signalAvailable << '\n';

        std::cout
            << "\n--- TS Queue ---\n"
            << "Accepted blocks: "
            << queue.acceptedBlocks << '\n'
            << "Accepted bytes: "
            << queue.acceptedBytes << '\n'
            << "Popped blocks: "
            << queue.poppedBlocks << '\n'
            << "Popped bytes: "
            << queue.poppedBytes << '\n'
            << "Queued blocks: "
            << queue.queuedBlocks << '\n'
            << "Queued bytes: "
            << queue.queuedBytes << '\n'
            << "Peak blocks: "
            << queue.peakBlocks << '\n'
            << "Peak bytes: "
            << queue.peakBytes << '\n'
            << "Dropped blocks: "
            << queue.droppedBlocks << '\n'
            << "Dropped bytes: "
            << queue.droppedBytes << '\n'
            << "Overflow blocks: "
            << queue.overflowBlocks << '\n'
            << "Invalid blocks: "
            << queue.invalidBlocks << '\n'
            << "Rejected closed blocks: "
            << queue.rejectedClosedBlocks << '\n'
            << "Discarded blocks: "
            << queue.discardedBlocks << '\n'
            << "Queue closed: "
            << queue.closed << '\n';

        std::cout
            << "\n--- HLS Consumer ---\n"
            << "Consumed blocks: "
            << consumer.consumedBlocks << '\n'
            << "Consumed bytes: "
            << consumer.consumedBytes << '\n'
            << "Forwarded blocks: "
            << consumer.forwardedBlocks << '\n'
            << "Forwarded bytes: "
            << consumer.forwardedBytes << '\n'
            << "Discontinuities: "
            << consumer.discontinuities << '\n'
            << "Queue timeouts: "
            << consumer.queueTimeouts << '\n'
            << "Write errors: "
            << consumer.writeErrors << '\n'
            << "Buffer update errors: "
            << consumer.bufferUpdateErrors << '\n'
            << "Process errors: "
            << consumer.processErrors << '\n'
            << "Playlist updates: "
            << consumer.playlistUpdates << '\n'
            << "Registered segments: "
            << consumer.registeredSegments << '\n';

        std::cout
            << "\n--- Circular Disk Buffer ---\n"
            << "Segment count: "
            << consumer.buffer.segmentCount << '\n'
            << "Storage bytes: "
            << consumer.buffer.storageBytes << '\n'
            << "Duration ms: "
            << consumer.buffer.durationMs << '\n'
            << "Total added segments: "
            << consumer.buffer.totalAddedSegments << '\n'
            << "Total removed segments: "
            << consumer.buffer.totalRemovedSegments << '\n'
            << "Pending deletions: "
            << consumer.pendingDeletionCount << '\n'
            << "Total storage bytes: "
            << consumer.totalStorageBytes << '\n';
    }

    std::cout
        << "\n--- Playback Manager ---\n"
        << "Playback state: "
        << playbackStateToString(playback.state) << '\n'
        << "Buffer available: "
        << playback.bufferAvailable << '\n'
        << "Oldest position ms: "
        << playback.oldestPositionMs << '\n'
        << "Live position ms: "
        << playback.livePositionMs << '\n'
        << "Playback position ms: "
        << playback.playbackPositionMs << '\n'
        << "Published buffer duration ms: "
        << playback.bufferDurationMs << '\n'
        << "Delay from live ms: "
        << playback.delayFromLiveMs << '\n'
        << "Position in buffer ms: "
        << playback.positionInBufferMs << '\n'
        << "At live edge: "
        << playback.atLiveEdge << '\n'
        << "Position expired: "
        << playback.positionExpired << '\n'
        << "Seek required: "
        << playback.seekRequired << '\n'
        << "Seek revision: "
        << playback.seekRevision << '\n'
        << "Playback error: "
        << (playback.errorMessage.empty()
                ? "NONE"
                : playback.errorMessage)
        << '\n'
        << "Session error: "
        << (session.errorMessage.empty()
                ? "NONE"
                : session.errorMessage)
        << '\n';

    std::cout
        << "\n--- Control Command Queue ---\n"
        << "Pending commands: "
        << commands.size() << '\n'
        << "Queue capacity: "
        << commands.capacity() << '\n'
        << "Queue closed: "
        << commands.isClosed() << '\n';

    std::cout
        << "\n--- Control Worker ---\n"
        << "Worker running: "
        << worker.isRunning() << '\n'
        << "Processed: "
        << workerStats.processed << '\n'
        << "Completed: "
        << workerStats.completed << '\n'
        << "Rejected: "
        << workerStats.rejected << '\n'
        << "Failed: "
        << workerStats.failed << '\n'
        << "Callback errors: "
        << workerStats.callbackErrors << '\n';

    std::cout
        << "\n--- Event Queue ---\n"
        << "Published events: "
        << eventStats.published << '\n'
        << "Consumed events: "
        << eventStats.consumed << '\n'
        << "Dropped events: "
        << eventStats.dropped << '\n'
        << "Queued events: "
        << eventStats.queued << '\n'
        << "Peak queued events: "
        << eventStats.peakQueued << '\n'
        << "Rejected closed events: "
        << eventStats.rejectedClosed << '\n'
        << "=====================================\n";
}

const char* submitStatusToString(
    CommandSubmitStatus status
)
{
    switch (status)
    {
        case CommandSubmitStatus::Accepted:
            return "ACCEPTED";

        case CommandSubmitStatus::Full:
            return "QUEUE_FULL";

        case CommandSubmitStatus::Closed:
            return "QUEUE_CLOSED";

        case CommandSubmitStatus::IdExhausted:
            return "OPERATION_ID_EXHAUSTED";
    }

    return "UNKNOWN";
}

void publishStateEvents(
    EventQueue& events,
    const ApiStateSnapshot& previous,
    const ApiStateSnapshot& current
)
{
    const auto& session = current.session;
    const auto& playback = session.playback;
    const auto& oldPlayback = previous.session.playback;

    const bool newGeneration =
        !previous.initialized ||
        previous.session.generationId !=
            session.generationId;

    ApiJson::Context context;

    context.generationId = session.generationId;

    context.sessionId =
        session.activeService
            ? session.generationId
            : 0;

    const auto publish =
        [&](const std::string& type,
            const ApiJson::Json& payload)
        {
            events.publish(
                ApiJson::event(type, context, payload)
            );
        };

    const auto sessionData = ApiJson::session(session);
    const auto playbackData = ApiJson::playback(playback);

    if (newGeneration && session.activeService)
    {
        publish("CHANNEL_STARTING", sessionData);
    }

    if (session.state == SessionState::Error &&
        (newGeneration ||
         previous.session.state != SessionState::Error))
    {
        publish("CHANNEL_FAILED", sessionData);

        publish("ERROR", {
            {"code", "SESSION_ERROR"},
            {"message", session.errorMessage},
            {"retryable", true}
        });
    }

    if (current.statistics)
    {
        const auto& producer =
            current.statistics->producer;

        const bool previousSignal =
            !newGeneration &&
            previous.statistics &&
            previous.statistics->producer.signalAvailable;

        if (previousSignal && !producer.signalAvailable)
        {
            publish("RTP_SIGNAL_LOST", {
                {"signalAvailable", false}
            });
        }

        if (!newGeneration &&
            previous.statistics &&
            !previousSignal &&
            producer.signalAvailable &&
            previous.statistics->producer.validRtpPackets > 0)
        {
            publish("RTP_SIGNAL_RECOVERED", {
                {"signalAvailable", true}
            });
        }

        if (producer.resolvedPmtTables > 0 &&
            (newGeneration ||
             !previous.statistics ||
             previous.statistics->producer.resolvedPmtTables !=
                 producer.resolvedPmtTables ||
             previous.statistics->producer.programs !=
                 producer.programs))
        {
            publish("PSI_UPDATED", {
                {"detectedPrograms", producer.detectedPrograms},
                {"resolvedPmtTables", producer.resolvedPmtTables}
            });
        }
    }

    if (playback.bufferAvailable &&
        (newGeneration || !oldPlayback.bufferAvailable))
    {
        publish("CHANNEL_READY", sessionData);
        publish("PLAYBACK_READY", playbackData);
    }

    if (newGeneration ||
        playback.bufferAvailable != oldPlayback.bufferAvailable ||
        playback.oldestPositionMs != oldPlayback.oldestPositionMs ||
        playback.livePositionMs != oldPlayback.livePositionMs)
    {
        publish("BUFFER_WINDOW_UPDATED", playbackData);
    }

    if (newGeneration ||
        playback.state != oldPlayback.state ||
        playback.playbackPositionMs !=
            oldPlayback.playbackPositionMs ||
        playback.seekRequired != oldPlayback.seekRequired ||
        playback.seekRevision != oldPlayback.seekRevision)
    {
        publish("PLAYBACK_POSITION", playbackData);
    }

    if (playback.positionExpired &&
        (newGeneration ||
         !oldPlayback.positionExpired ||
         playback.seekRevision != oldPlayback.seekRevision))
    {
        publish("BUFFER_POSITION_CLAMPED", playbackData);
    }
}

void drainEvents(EventQueue& events)
{
    while (const auto event = events.tryPop())
    {
        std::cout
            << "\n[EVENT] "
            << event->dump()
            << '\n';
    }
}

void processCommand(
    const std::string& line,
    SessionController& controller,
    const ServiceManager& services,
    ControlCommandQueue& commands,
    const ControlWorker& worker,
    EventQueue& events,
    std::uint64_t generationId,
    std::uint64_t& requestCounter
)
{
    std::istringstream input(line);

    std::string value;

    if (!(input >> value))
    {
        return;
    }

    ControlCommand command;

    if (value == "seek")
    {
        if (!(input >> command.seekPositionMs))
        {
            std::cerr
                << "[WARNING] Usage: seek MS\n";

            return;
        }

        command.type = ControlCommandType::Seek;
    }

    std::string extra;

    if (input >> extra)
    {
        std::cerr << "[WARNING] Invalid command\n";

        return;
    }

    if (value == "q")
    {
        stopRequested = 1;
        return;
    }

    if (value == "h")
    {
        printCommands();
        return;
    }

    if (value == "s")
    {
        printStatistics(
            controller,
            commands,
            worker,
            events
        );

        return;
    }

    if (value == "l")
    {
        printServices(services);
        return;
    }

    if (value == "n")
    {
        command.type =
            ControlCommandType::NextChannel;
    }
    else if (value == "p")
    {
        command.type =
            ControlCommandType::PreviousChannel;
    }
    else if (value == "pause")
    {
        command.type =
            ControlCommandType::Pause;
    }
    else if (value == "resume")
    {
        command.type =
            ControlCommandType::Resume;
    }
    else if (value == "live")
    {
        command.type =
            ControlCommandType::GoLive;
    }
    else if (value == "oldest")
    {
        command.type =
            ControlCommandType::GoOldest;
    }
    else if (value != "seek")
    {
        unsigned int channelNumber = 0;

        std::istringstream numberInput(value);

        if (value.find_first_not_of("0123456789")
                != std::string::npos ||
            !(numberInput >> channelNumber) ||
            !numberInput.eof() ||
            channelNumber == 0)
        {
            std::cerr
                << "[WARNING] Unknown command: "
                << value << '\n';

            return;
        }

        command.type =
            ControlCommandType::SelectChannel;

        command.channelNumber = channelNumber;
    }

    command.generationId = generationId;

    command.requestId =
        "cli-" + std::to_string(++requestCounter);

    const auto submitted = commands.submit(command);

    if (!submitted.accepted())
    {
        std::cerr
            << "[WARNING] Command rejected: "
            << submitStatusToString(submitted.status)
            << '\n';

        return;
    }

    std::cout
        << "[ACCEPTED] "
        << controlCommandTypeToString(command.type)
        << ", requestId: "
        << command.requestId
        << ", operationId: "
        << submitted.operationId
        << ", generationId: "
        << command.generationId
        << '\n';
}

void pollCommands(
    SessionController& controller,
    const ServiceManager& services,
    ControlCommandQueue& commands,
    const ControlWorker& worker,
    EventQueue& events,
    std::string& pendingInput,
    bool& inputAvailable,
    std::uint64_t generationId,
    std::uint64_t& requestCounter
)
{
    pollfd descriptor{};

    descriptor.fd =
        inputAvailable ? STDIN_FILENO : -1;

    descriptor.events = POLLIN;

    const int result = ::poll(
        &descriptor,
        1,
        static_cast<int>(CONTROL_INTERVAL.count())
    );

    if (result < 0)
    {
        if (errno != EINTR)
        {
            inputAvailable = false;
        }

        return;
    }

    if (result == 0 || stopRequested)
    {
        return;
    }

    const auto handleLine =
        [&](const std::string& line)
        {
            processCommand(
                line,
                controller,
                services,
                commands,
                worker,
                events,
                generationId,
                requestCounter
            );
        };

    if (descriptor.revents & (POLLIN | POLLHUP))
    {
        char buffer[256];

        const ssize_t count = ::read(
            STDIN_FILENO,
            buffer,
            sizeof(buffer)
        );

        if (count == 0)
        {
            if (!pendingInput.empty())
            {
                handleLine(pendingInput);
                pendingInput.clear();
            }

            inputAvailable = false;
            return;
        }

        if (count < 0)
        {
            if (errno != EINTR && errno != EAGAIN)
            {
                inputAvailable = false;
            }

            return;
        }

        pendingInput.append(
            buffer,
            static_cast<std::size_t>(count)
        );

        std::size_t newline;

        while (!stopRequested &&
               (newline = pendingInput.find('\n'))
                   != std::string::npos)
        {
            const std::string line =
                pendingInput.substr(0, newline);

            pendingInput.erase(0, newline + 1);

            handleLine(line);
        }

        if (pendingInput.size() > 4096)
        {
            pendingInput.clear();

            std::cerr
                << "[WARNING] Input too long\n";
        }
    }

    if (descriptor.revents & (POLLERR | POLLNVAL))
    {
        inputAvailable = false;
    }
}

} // namespace

int main()
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    std::cout << std::boolalpha;

    try
    {
        std::cout
            << "[INFO] Loading TV services from OTT Middleware\n";

        OttMetadataManager ottMetadata;

        auto catalog = ottMetadata.getServices();

        if (!catalog.success())
        {
            std::cerr
                << "[ERROR] Failed to load OTT service catalog: "
                << ottRestStatusToString(catalog.networkStatus)
                << " - "
                << catalog.errorMessage
                << '\n';

            return 1;
        }

        std::cout
            << "[INFO] OTT metadata source: "
            << ottMetadataSourceToString(catalog.source)
            << '\n'
            << "[INFO] OTT request attempts: "
            << catalog.attempts
            << '\n';

        if (stopRequested)
        {
            return 0;
        }

        const ServiceManager services(
            std::move(catalog.data)
        );

        if (services.empty())
        {
            std::cerr
                << "[ERROR] OTT service catalog is empty\n";

            return 1;
        }

        std::cout
            << "[INFO] OTT service catalog loaded: "
            << services.serviceCount()
            << " services\n";

        HlsConfig hlsConfig;

        hlsConfig.outputDirectory = "runtime/hls";
        hlsConfig.segmentDurationSeconds = 2;
        hlsConfig.playlistSize = 60;
        hlsConfig.deleteThreshold = 2;
        hlsConfig.playlistFilename = "producer.m3u8";
        hlsConfig.deleteSegments = false;

        BufferConfig bufferConfig;

        bufferConfig.storageDirectory =
            hlsConfig.outputDirectory;

        bufferConfig.maxDurationMs =
            30ULL * 60ULL * 1000ULL;

        bufferConfig.maxStorageBytes =
            15ULL * 1024ULL * 1024ULL * 1024ULL;

        bufferConfig.maxTotalStorageBytes =
            20ULL * 1024ULL * 1024ULL * 1024ULL;

        bufferConfig.deletionMode =
            SegmentDeletionMode::Deferred;

        bufferConfig.deletionGrace =
            std::chrono::milliseconds(30000);

	std::filesystem::remove_all(
	    hlsConfig.outputDirectory
	);

	std::filesystem::create_directories(
	    hlsConfig.outputDirectory
	);

        SessionController controller(
            services,
            hlsConfig,
            bufferConfig
        );

        ControlCommandQueue commands(128);
        EventQueue events(256);
	ApiStateCache stateCache;

	ApiRequestHandler apiHandler(
	    services,
	    stateCache,
	    ottMetadata,
	    commands
	);

	HttpServer httpServer(apiHandler, events, 8080);

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
                        "Operation event publication failed"
                    );
                }
            }
        );

        bool failed = false;

        std::string failureMessage;

        try
        {
            if (!controller.start())
            {
                std::cerr
                    << "[ERROR] Startup failed: "
                    << controller.snapshot().errorMessage
                    << '\n';

                return 1;
            }

            if (!worker.start())
            {
                controller.stop();

                std::cerr
                    << "[ERROR] Control worker startup failed\n";

                return 1;
            }

            std::cout
                << "[INFO] Asynchronous control pipeline started\n";

	stateCache.publish(
	    controller.snapshot(),
	    controller.statistics()
	);

	if (!httpServer.start())
	{
	    worker.stop();
	    controller.stop();

	    std::cerr
	        << "[ERROR] HTTP server startup failed\n";

	    return 1;
	}

            printCommands();
            printServices(services);

            auto lastStatistics =
                std::chrono::steady_clock::now();

            bool inputAvailable = true;

            std::string pendingInput;

            std::uint64_t requestCounter = 0;

	    ApiStateSnapshot previousState;

            while (!stopRequested)
            {
		controller.update();

                if (!worker.isRunning())
                {
                    failed = true;

                    failureMessage =
                        "Control worker stopped unexpectedly";

                    break;
                }

		if (!httpServer.isRunning())
		{
		    failed = true;
		    failureMessage = "HTTP server stopped unexpectedly";
		    break;
		}

		const auto session = controller.snapshot();

		const auto statistics = controller.statistics();

		if (controller.snapshot().generationId ==
		    session.generationId)
		{
		    if (stateCache.publish(session, statistics))
		    {
		        const auto currentState =
		            stateCache.snapshot();

		        publishStateEvents(
		            events,
		            previousState,
		            currentState
		        );

		        previousState = currentState;
		    }
		}
		const auto generationId = session.generationId;

                pollCommands(
                    controller,
                    services,
                    commands,
                    worker,
                    events,
                    pendingInput,
                    inputAvailable,
                    generationId,
                    requestCounter
                );

                const auto now =
                    std::chrono::steady_clock::now();

                if (now - lastStatistics >=
                    STATISTICS_INTERVAL)
                {
                    printStatistics(
                        controller,
                        commands,
                        worker,
                        events
                    );

                    lastStatistics = now;
                }
            }
        }
        catch (const std::exception& error)
        {
            failed = true;
            failureMessage = error.what();
        }
        catch (...)
        {
            failed = true;
            failureMessage = "Unknown error";
        }

	httpServer.stop();

        std::cout
            << "[INFO] Stopping control pipeline\n";

        worker.stop();

        events.close();

        printStatistics(
            controller,
            commands,
            worker,
            events
        );

        const auto workerStats = worker.statistics();
        const auto eventStats = events.statistics();

        const auto beforeStop = controller.snapshot();

        if (beforeStop.state == SessionState::Error)
        {
            failed = true;

            if (failureMessage.empty())
            {
                failureMessage =
                    beforeStop.errorMessage;
            }
        }

	if (eventStats.dropped != 0)
	{
	    std::cerr
	        << "[WARNING] Dropped WebSocket events: "
	        << eventStats.dropped << '\n';
	}

	if (workerStats.callbackErrors != 0)
        {
            failed = true;

            if (!failureMessage.empty())
            {
                failureMessage += "; ";
            }

            failureMessage +=
                "Control operation events were lost";
        }

        controller.stop();

        const auto finalSnapshot =
            controller.snapshot();

        std::cout
            << "Final session state: "
            << sessionStateToString(finalSnapshot.state)
            << '\n'
            << "Final playback state: "
            << playbackStateToString(
                finalSnapshot.playback.state
            )
            << '\n';

        if (failed ||
            finalSnapshot.state == SessionState::Error)
        {
            if (!finalSnapshot.errorMessage.empty())
            {
                if (!failureMessage.empty())
                {
                    failureMessage += "; ";
                }

                failureMessage +=
                    finalSnapshot.errorMessage;
            }

            std::cerr
                << "[ERROR] Receiver stopped with error: "
                << failureMessage
                << '\n';

            return 1;
        }

        std::cout
            << "[INFO] Receiver stopped successfully\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "[ERROR] Backend initialization failed: "
            << error.what()
            << '\n';

        return 1;
    }
    catch (...)
    {
        std::cerr
            << "[ERROR] Unknown initialization error\n";

        return 1;
    }
}
