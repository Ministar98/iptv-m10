#include "SessionController.hpp"

#include "OttMetadataManager.hpp"
#include <utility>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

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
        << "n     - Next channel\n"
        << "p     - Previous channel\n"
        << "NUM   - Select channel\n"
        << "s     - Statistics\n"
        << "l     - Service list\n"
        << "h     - Help\n"
        << "q     - Quit\n"
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
    const SessionController& controller
)
{
    const auto session = controller.snapshot();

    const auto statistics = controller.statistics();

    const auto& playback = session.playback;

    std::cout
        << "\n========== IPTV Statistics ==========\n"
        << "Session state: "
        << sessionStateToString(session.state)
        << '\n'
        << "Generation ID: "
        << session.generationId
        << '\n';

    if (session.activeService)
    {
        std::cout
            << "Active service: "
            << session.activeService->name
            << '\n'
            << "Service ID: "
            << session.activeService->serviceId
            << '\n'
            << "Channel number: "
            << session.activeService->channelNumber
            << '\n';
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
        << playbackStateToString(playback.state)
        << '\n'
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
        << '\n'
        << "=====================================\n";
}

void processCommand(
    const std::string& command,
    SessionController& controller,
    const ServiceManager& services
)
{
    std::istringstream input(command);

    std::string value;

    if (!(input >> value))
    {
        return;
    }

    std::string extra;

    if (input >> extra)
    {
        std::cerr << "[WARNING] Invalid command\n";

        return;
    }

    bool success = false;

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
        printStatistics(controller);

        return;
    }

    if (value == "l")
    {
        printServices(services);

        return;
    }

    if (value == "n")
    {
        success = controller.nextChannel();
    }
    else if (value == "p")
    {
        success = controller.previousChannel();
    }
    else
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

        success =
            controller.selectChannel(channelNumber);
    }

    if (!success)
    {
        std::cerr
            << "[WARNING] Channel selection failed\n";

        return;
    }

    const auto snapshot = controller.snapshot();

    if (snapshot.activeService)
    {
        std::cout
            << "[INFO] Selected channel: "
            << snapshot.activeService->channelNumber
            << " - "
            << snapshot.activeService->name
            << ", generation: "
            << snapshot.generationId
            << '\n';
    }
}

void pollCommands(
    SessionController& controller,
    const ServiceManager& services,
    std::string& pendingInput,
    bool& inputAvailable
)
{
    pollfd descriptor{};

    descriptor.fd =
        inputAvailable ? STDIN_FILENO : -1;

    descriptor.events = POLLIN;

    const int result = ::poll(
        &descriptor,
        1,
        static_cast<int>(
            CONTROL_INTERVAL.count()
        )
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
                processCommand(
                    pendingInput,
                    controller,
                    services
                );

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
            const std::string command =
                pendingInput.substr(0, newline);

            pendingInput.erase(0, newline + 1);

            processCommand(
                command,
                controller,
                services
            );
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

}

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

        SessionController controller(
            services,
            hlsConfig,
            bufferConfig
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

            printCommands();

            printServices(services);

            auto lastStatistics =
                std::chrono::steady_clock::now();

            bool inputAvailable = true;

            std::string pendingInput;

            while (!stopRequested)
            {
                if (!controller.update())
                {
                    failed = true;

                    failureMessage =
                        controller.snapshot().errorMessage;

                    break;
                }

                pollCommands(
                    controller,
                    services,
                    pendingInput,
                    inputAvailable
                );

                const auto now =
                    std::chrono::steady_clock::now();

                if (now - lastStatistics >=
                    STATISTICS_INTERVAL)
                {
                    printStatistics(controller);

                    lastStatistics = now;
                }
            }
        }
        catch (const std::exception& error)
        {
            failed = true;

            failureMessage = error.what();

            std::cerr
                << "[ERROR] "
                << failureMessage
                << '\n';
        }
        catch (...)
        {
            failed = true;

            failureMessage = "Unknown error";

            std::cerr
                << "[ERROR] "
                << failureMessage
                << '\n';
        }

        printStatistics(controller);

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
