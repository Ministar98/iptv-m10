#include "BoundedTsQueue.hpp"
#include "RtpProducer.hpp"
#include "HlsConsumer.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <thread>

namespace
{

constexpr std::uint16_t RTP_PORT = 5004;

constexpr std::size_t QUEUE_MAX_BYTES =
    8U * 1024U * 1024U;

constexpr std::size_t QUEUE_MAX_BLOCKS =
    4096U;

constexpr std::chrono::milliseconds
    CONTROL_INTERVAL{100};

constexpr std::chrono::seconds
    STATISTICS_INTERVAL{5};

volatile std::sig_atomic_t stopRequested = 0;

void handleSignal(int)
{
    stopRequested = 1;
}

void printStatistics(
    const RtpProducer& producer,
    const HlsConsumer& consumer,
    const BoundedTsQueue& queue
)
{
    const auto producerStats =
        producer.statistics();

    const auto consumerStats =
        consumer.statistics();

    const auto queueStats =
        queue.statistics();

    std::ostringstream output;

    output
        << "\n========== IPTV STATISTICS ==========\n"

        << "\n--- RTP Producer ---\n"

        << "Received datagrams: "
        << producerStats.receivedDatagrams << '\n'

        << "Valid RTP packets: "
        << producerStats.validRtpPackets << '\n'

        << "Invalid RTP packets: "
        << producerStats.invalidRtpPackets << '\n'

        << "Unexpected SSRC packets: "
        << producerStats.unexpectedSsrcPackets << '\n'

        << "RTP discontinuities: "
        << producerStats.rtpDiscontinuities << '\n'

        << "Valid TS packets: "
        << producerStats.validTsPackets << '\n'

        << "Invalid TS packets: "
        << producerStats.invalidTsPackets << '\n'

        << "Transport errors: "
        << producerStats.transportErrors << '\n'

        << "Valid PAT sections: "
        << producerStats.validPatSections << '\n'

        << "Invalid PAT sections: "
        << producerStats.invalidPatSections << '\n'

        << "Valid PMT sections: "
        << producerStats.validPmtSections << '\n'

        << "Invalid PMT sections: "
        << producerStats.invalidPmtSections << '\n'

        << "Unexpected PMT sections: "
        << producerStats.unexpectedPmtSections << '\n'

        << "Detected programs: "
        << producerStats.detectedPrograms << '\n'

        << "Resolved PMT tables: "
        << producerStats.resolvedPmtTables << '\n'

        << "Queued blocks: "
        << producerStats.queuedBlocks << '\n'

        << "Queued bytes: "
        << producerStats.queuedBytes << '\n'

        << "Producer dropped blocks: "
        << producerStats.droppedBlocks << '\n'

        << "Producer dropped bytes: "
        << producerStats.droppedBytes << '\n'

        << "Receive timeouts: "
        << producerStats.receiveTimeouts << '\n'

        << "Receive errors: "
        << producerStats.receiveErrors << '\n'

        << "Signal available: "
        << (producerStats.signalAvailable
                ? "YES"
                : "NO")
        << '\n'

        << "\n--- Bounded TS Queue ---\n"

        << "Accepted blocks: "
        << queueStats.acceptedBlocks << '\n'

        << "Accepted bytes: "
        << queueStats.acceptedBytes << '\n'

        << "Popped blocks: "
        << queueStats.poppedBlocks << '\n'

        << "Popped bytes: "
        << queueStats.poppedBytes << '\n'

        << "Current blocks: "
        << queueStats.queuedBlocks << '\n'

        << "Current bytes: "
        << queueStats.queuedBytes << '\n'

        << "Peak blocks: "
        << queueStats.peakBlocks << '\n'

        << "Peak bytes: "
        << queueStats.peakBytes << '\n'

        << "Dropped blocks: "
        << queueStats.droppedBlocks << '\n'

        << "Dropped bytes: "
        << queueStats.droppedBytes << '\n'

        << "Overflow blocks: "
        << queueStats.overflowBlocks << '\n'

        << "Invalid blocks: "
        << queueStats.invalidBlocks << '\n'

        << "Rejected closed blocks: "
        << queueStats.rejectedClosedBlocks << '\n'

        << "Discarded blocks: "
        << queueStats.discardedBlocks << '\n'

        << "Queue closed: "
        << (queueStats.closed ? "YES" : "NO")
        << '\n'

        << "\n--- HLS Consumer ---\n"

        << "Consumed blocks: "
        << consumerStats.consumedBlocks << '\n'

        << "Consumed bytes: "
        << consumerStats.consumedBytes << '\n'

        << "Forwarded blocks: "
        << consumerStats.forwardedBlocks << '\n'

        << "Forwarded bytes: "
        << consumerStats.forwardedBytes << '\n'

        << "Discontinuities: "
        << consumerStats.discontinuities << '\n'

        << "Queue timeouts: "
        << consumerStats.queueTimeouts << '\n'

        << "HLS write errors: "
        << consumerStats.writeErrors << '\n'

        << "Buffer update errors: "
        << consumerStats.bufferUpdateErrors << '\n'

        << "FFmpeg process errors: "
        << consumerStats.processErrors << '\n'

        << "Playlist updates: "
        << consumerStats.playlistUpdates << '\n'

        << "Registered segments: "
        << consumerStats.registeredSegments << '\n'

        << "\n--- Circular Disk Buffer ---\n"

        << "Buffer duration ms: "
        << consumerStats.buffer.durationMs << '\n'

        << "Active segments: "
        << consumerStats.buffer.segmentCount << '\n'

        << "Pending deletion: "
        << consumerStats.pendingDeletionCount << '\n'

        << "Total registered bytes: "
        << consumerStats.totalStorageBytes << '\n'

        << "=====================================\n";

    std::cout << output.str();
}

} // namespace

int main()
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGPIPE, SIG_IGN);

    std::cout
        << "IPTV M10 Producer-Consumer Receiver\n\n";

    // Shared bounded queue.

    BoundedTsQueue queue(
        QUEUE_MAX_BYTES,
        QUEUE_MAX_BLOCKS
    );

    // HLS configuration.

    HlsConfig hlsConfig{};

    hlsConfig.outputDirectory =
        "runtime/hls";

    hlsConfig.segmentDurationSeconds = 2;

    hlsConfig.playlistSize = 60;

    hlsConfig.deleteThreshold = 2;

    hlsConfig.playlistFilename =
        "producer.m3u8";

    hlsConfig.deleteSegments = false;

    // Circular disk buffer configuration.

    BufferConfig bufferConfig{};

    bufferConfig.storageDirectory =
        hlsConfig.outputDirectory;

    bufferConfig.deletionMode =
        SegmentDeletionMode::Deferred;

    bufferConfig.maxDurationMs =
        30ULL * 60ULL * 1000ULL;

    // Worker components.

    RtpProducer producer(
        queue,
        RTP_PORT
    );

    HlsConsumer consumer(
        queue,
        hlsConfig,
        bufferConfig
    );

    // Initialize UDP socket.

    if (!producer.initialize())
    {
        std::cerr
            << "Failed to initialize RTP producer: "
            << producer.lastError() << '\n';

        return 1;
    }

    // Start FFmpeg before creating worker threads.

    if (!consumer.initialize())
    {
        std::cerr
            << "Failed to initialize HLS consumer: "
            << consumer.lastError() << '\n';

        return 1;
    }

    std::thread producerThread;
    std::thread consumerThread;

    bool coordinatorError = false;

    try
    {
        // Start consumer first.

        consumerThread = std::thread(
            &HlsConsumer::run,
            &consumer
        );

        producerThread = std::thread(
            &RtpProducer::run,
            &producer
        );

        std::cout
            << "\n[INFO] Worker threads started\n"
            << "Queue capacity: "
            << QUEUE_MAX_BYTES << " bytes\n"
            << "Queue block limit: "
            << QUEUE_MAX_BLOCKS << '\n'
            << "Press Ctrl+C to stop\n";

        auto lastStatistics =
            std::chrono::steady_clock::now();

        // Main thread coordinates the workers.

        while (!stopRequested)
        {
            if (
                producer.hasFatalError() ||
                consumer.hasFatalError()
            )
            {
                break;
            }

            if (
                producer.isFinished() ||
                consumer.isFinished()
            )
            {
                coordinatorError = true;

                std::cerr
                    << "[ERROR] Worker finished "
                    << "unexpectedly\n";

                break;
            }

            const auto now =
                std::chrono::steady_clock::now();

            if (
                now - lastStatistics >=
                STATISTICS_INTERVAL
            )
            {
                printStatistics(
                    producer,
                    consumer,
                    queue
                );

                lastStatistics = now;
            }

            std::this_thread::sleep_for(
                CONTROL_INTERVAL
            );
        }
    }
    catch (const std::exception& error)
    {
        coordinatorError = true;

        std::cerr
            << "[ERROR] Coordinator exception: "
            << error.what() << '\n';
    }
    catch (...)
    {
        coordinatorError = true;

        std::cerr
            << "[ERROR] Unknown coordinator exception\n";
    }

    // Shutdown sequence.

    std::cout
        << "\n[INFO] Stopping IPTV receiver...\n";

    producer.requestStop();

    if (
        coordinatorError ||
        producer.hasFatalError() ||
        consumer.hasFatalError()
    )
    {
        queue.abort();
    }

    // Stop producer before closing the queue.

    if (producerThread.joinable())
    {
        producerThread.join();
    }

    // Wake consumer and allow remaining data to drain.

    queue.close();

    if (consumerThread.joinable())
    {
        consumerThread.join();
    }

    // Both worker threads have finished.

    printStatistics(
        producer,
        consumer,
        queue
    );

    if (producer.hasFatalError())
    {
        std::cerr
            << "[ERROR] Producer: "
            << producer.lastError() << '\n';
    }

    if (consumer.hasFatalError())
    {
        std::cerr
            << "[ERROR] Consumer: "
            << consumer.lastError() << '\n';
    }

    if (
        coordinatorError ||
        producer.hasFatalError() ||
        consumer.hasFatalError()
    )
    {
        std::cerr
            << "Receiver stopped due to error\n";

        return 1;
    }

    std::cout
        << "Receiver stopped successfully\n";

    return 0;
}
