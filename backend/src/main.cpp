#include "BoundedTsQueue.hpp"
#include "RtpProducer.hpp"
#include "HlsConsumer.hpp"
#include "PlaybackManager.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{

constexpr std::uint16_t RTP_PORT = 5004;

constexpr std::size_t QUEUE_MAX_BYTES =
    8U * 1024U * 1024U;

constexpr std::size_t QUEUE_MAX_BLOCKS = 4096U;

constexpr std::uint64_t GENERATION_ID = 1;

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
    const BoundedTsQueue& queue,
    const PlaybackManager& playback
)
{
    const auto producerStats = producer.statistics();
    const auto consumerStats = consumer.statistics();
    const auto queueStats = queue.statistics();
    const auto playbackStats = playback.snapshot();

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
        << (producerStats.signalAvailable ? "YES" : "NO")
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

        << "\n--- Playback Manager ---\n"

        << "Playback state: "
        << playbackStateToString(playbackStats.state)
        << '\n'

        << "Generation ID: "
        << playbackStats.generationId << '\n'

        << "Buffer available: "
        << (playbackStats.bufferAvailable ? "YES" : "NO")
        << '\n'

        << "Oldest position ms: "
        << playbackStats.oldestPositionMs << '\n'

        << "Live position ms: "
        << playbackStats.livePositionMs << '\n'

        << "Playback position ms: "
        << playbackStats.playbackPositionMs << '\n'

        << "Published buffer duration ms: "
        << playbackStats.bufferDurationMs << '\n'

        << "Delay from live ms: "
        << playbackStats.delayFromLiveMs << '\n'

        << "Position in buffer ms: "
        << playbackStats.positionInBufferMs << '\n'

        << "At live edge: "
        << (playbackStats.atLiveEdge ? "YES" : "NO")
        << '\n'

        << "Position expired: "
        << (playbackStats.positionExpired ? "YES" : "NO")
        << '\n'

        << "Seek required: "
        << (playbackStats.seekRequired ? "YES" : "NO")
        << '\n'

        << "Seek revision: "
        << playbackStats.seekRevision << '\n'

        << "Playback error: "
        << (
            playbackStats.errorMessage.empty()
                ? "NONE"
                : playbackStats.errorMessage
        )
        << '\n'

        << "=====================================\n";

    std::cout << output.str();
}

bool synchronizePlayback(
    const HlsConsumer& consumer,
    PlaybackManager& playback
)
{
    return playback.updateBufferInterval(
        GENERATION_ID,
        consumer.publishedInterval()
    );
}

} // namespace

int main()
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGPIPE, SIG_IGN);

    std::cout
        << "IPTV M10 Producer-Consumer Receiver\n\n";

    BoundedTsQueue queue(
        QUEUE_MAX_BYTES,
        QUEUE_MAX_BLOCKS
    );

    HlsConfig hlsConfig{};

    hlsConfig.outputDirectory = "runtime/hls";
    hlsConfig.segmentDurationSeconds = 2;
    hlsConfig.playlistSize = 60;
    hlsConfig.deleteThreshold = 2;
    hlsConfig.playlistFilename = "producer.m3u8";
    hlsConfig.deleteSegments = false;

    BufferConfig bufferConfig{};

    bufferConfig.storageDirectory =
        hlsConfig.outputDirectory;

    bufferConfig.deletionMode =
        SegmentDeletionMode::Deferred;

    bufferConfig.maxDurationMs =
        30ULL * 60ULL * 1000ULL;

    RtpProducer producer(
        queue,
        RTP_PORT
    );

    HlsConsumer consumer(
        queue,
        hlsConfig,
        bufferConfig
    );

    PlaybackManager playback(2000);

    playback.reset(GENERATION_ID);

    if (!producer.initialize())
    {
        playback.setError(producer.lastError());

        std::cerr
            << "Failed to initialize RTP producer: "
            << producer.lastError() << '\n';

        return 1;
    }

    if (!consumer.initialize())
    {
        playback.setError(consumer.lastError());

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
        if (!playback.start())
        {
            throw std::runtime_error(
                "Failed to start Playback Manager"
            );
        }

        std::cout
            << "[INFO] Playback Manager started: "
            << playbackStateToString(
                playback.snapshot().state
            )
            << '\n';

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

        while (!stopRequested)
        {
            if (producer.hasFatalError())
            {
                playback.setError(
                    producer.lastError()
                );

                break;
            }

            if (consumer.hasFatalError())
            {
                playback.setError(
                    consumer.lastError()
                );

                break;
            }

            if (
                producer.isFinished() ||
                consumer.isFinished()
            )
            {
                coordinatorError = true;

                playback.setError(
                    "Worker finished unexpectedly"
                );

                std::cerr
                    << "[ERROR] Worker finished "
                    << "unexpectedly\n";

                break;
            }

            if (!synchronizePlayback(
                    consumer,
                    playback
                ))
            {
                coordinatorError = true;

                playback.setError(
                    "Invalid published buffer interval"
                );

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
                    queue,
                    playback
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

        playback.setError(error.what());

        std::cerr
            << "[ERROR] Coordinator exception: "
            << error.what() << '\n';
    }
    catch (...)
    {
        coordinatorError = true;

        playback.setError(
            "Unknown coordinator exception"
        );

        std::cerr
            << "[ERROR] Unknown coordinator exception\n";
    }

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

    if (producerThread.joinable())
    {
        producerThread.join();
    }

    queue.close();

    if (consumerThread.joinable())
    {
        consumerThread.join();
    }

    if (!synchronizePlayback(
            consumer,
            playback
        ))
    {
        coordinatorError = true;

        playback.setError(
            "Invalid final buffer interval"
        );
    }

    if (producer.hasFatalError())
    {
        playback.setError(
            producer.lastError()
        );

        std::cerr
            << "[ERROR] Producer: "
            << producer.lastError() << '\n';
    }

    if (consumer.hasFatalError())
    {
        playback.setError(
            consumer.lastError()
        );

        std::cerr
            << "[ERROR] Consumer: "
            << consumer.lastError() << '\n';
    }

    const bool failed =
        coordinatorError ||
        producer.hasFatalError() ||
        consumer.hasFatalError();

    if (!failed)
    {
        playback.stop();
    }

    printStatistics(
        producer,
        consumer,
        queue,
        playback
    );

    if (failed)
    {
        std::cerr
            << "Receiver stopped due to error\n";

        return 1;
    }

    std::cout
        << "Receiver stopped successfully\n";

    return 0;
}
