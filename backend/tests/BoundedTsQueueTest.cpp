#include "BoundedTsQueue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

using Push = BoundedTsQueue::PushResult;
using Pop = BoundedTsQueue::PopResult;

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

TsDataBlock makeBlock(
    std::uint16_t sequence,
    std::size_t bytes = 188
)
{
    TsDataBlock block;

    block.data.resize(bytes, 0x47);
    block.rtpSequenceNumber = sequence;
    block.rtpTimestamp = sequence * 3600U;
    block.receivedAt = std::chrono::steady_clock::now();

    return block;
}

void testInvalidConfiguration()
{
    bool zeroBytesRejected = false;
    bool zeroBlocksRejected = false;

    try
    {
        BoundedTsQueue queue(0, 10);
    }
    catch (const std::invalid_argument&)
    {
        zeroBytesRejected = true;
    }

    try
    {
        BoundedTsQueue queue(1024, 0);
    }
    catch (const std::invalid_argument&)
    {
        zeroBlocksRejected = true;
    }

    require(zeroBytesRejected, "Zero byte limit");
    require(zeroBlocksRejected, "Zero block limit");

    std::cout << "[PASS] Invalid configuration\n";
}

void testFifoAndMove()
{
    BoundedTsQueue queue;

    auto first = makeBlock(1);
    auto second = makeBlock(2);

    first.discontinuity = true;

    const auto* originalData = first.data.data();
    const auto originalTime = first.receivedAt;

    require(
        queue.tryPush(std::move(first)) == Push::Accepted,
        "Push first block"
    );

    require(
        queue.tryPush(std::move(second)) == Push::Accepted,
        "Push second block"
    );

    TsDataBlock output;

    require(
        queue.waitPop(output, 100ms) == Pop::Data,
        "Pop first block"
    );

    require(
        output.rtpSequenceNumber == 1,
        "First sequence number"
    );

    require(
        output.data.data() == originalData,
        "Move ownership without copying"
    );

    require(
        output.receivedAt == originalTime,
        "Receive timestamp preservation"
    );

    require(
        output.rtpTimestamp == 3600U,
        "RTP timestamp preservation"
    );

    require(
        output.discontinuity,
        "Existing discontinuity preservation"
    );

    require(
        queue.waitPop(output, 100ms) == Pop::Data,
        "Pop second block"
    );

    require(
        output.rtpSequenceNumber == 2,
        "FIFO ordering"
    );

    require(
        !output.discontinuity,
        "Discontinuity isolation"
    );

    std::cout << "[PASS] FIFO, move and metadata\n";
}

void testByteLimit()
{
    BoundedTsQueue queue(376, 100);

    require(
        queue.tryPush(makeBlock(1)) == Push::Accepted,
        "Byte limit first block"
    );

    require(
        queue.tryPush(makeBlock(2)) == Push::Accepted,
        "Byte limit second block"
    );

    require(
        queue.tryPush(makeBlock(3)) == Push::Full,
        "Byte limit overflow"
    );

    const auto stats = queue.statistics();

    require(stats.queuedBytes == 376, "Queued bytes");
    require(stats.queuedBlocks == 2, "Queued blocks");
    require(stats.overflowBlocks == 1, "Byte overflow count");
    require(stats.droppedBytes == 188, "Dropped bytes");

    std::cout << "[PASS] Byte capacity limit\n";
}

void testBlockLimit()
{
    BoundedTsQueue queue(1024 * 1024, 2);

    require(
        queue.tryPush(makeBlock(1)) == Push::Accepted,
        "Block limit first block"
    );

    require(
        queue.tryPush(makeBlock(2)) == Push::Accepted,
        "Block limit second block"
    );

    require(
        queue.tryPush(makeBlock(3)) == Push::Full,
        "Block count overflow"
    );

    const auto stats = queue.statistics();

    require(stats.queuedBlocks == 2, "Block count limit");
    require(stats.overflowBlocks == 1, "Block overflow count");

    std::cout << "[PASS] Block count limit\n";
}

void testInvalidBlocks()
{
    BoundedTsQueue queue(376, 10);

    require(
        queue.tryPush(TsDataBlock{}) == Push::Invalid,
        "Empty block rejection"
    );

    require(
        queue.tryPush(makeBlock(1, 564)) == Push::Invalid,
        "Oversized block rejection"
    );

    require(
        queue.tryPush(makeBlock(2)) == Push::Accepted,
        "Valid block after invalid blocks"
    );

    TsDataBlock output;

    require(
        queue.waitPop(output, 100ms) == Pop::Data,
        "Pop valid block"
    );

    require(
        output.discontinuity,
        "Discontinuity after rejected blocks"
    );

    const auto stats = queue.statistics();

    require(stats.invalidBlocks == 2, "Invalid block count");
    require(stats.droppedBlocks == 2, "Invalid drop count");
    require(stats.droppedBytes == 564, "Invalid dropped bytes");

    std::cout << "[PASS] Invalid block rejection\n";
}

void testOverflowDiscontinuity()
{
    BoundedTsQueue queue(376, 2);

    require(
        queue.tryPush(makeBlock(1)) == Push::Accepted,
        "Push block 1"
    );

    require(
        queue.tryPush(makeBlock(2)) == Push::Accepted,
        "Push block 2"
    );

    require(
        queue.tryPush(makeBlock(3)) == Push::Full,
        "Drop block 3"
    );

    require(
        queue.tryPush(makeBlock(4)) == Push::Full,
        "Drop block 4"
    );

    TsDataBlock output;

    require(
        queue.waitPop(output, 100ms) == Pop::Data,
        "Pop block 1"
    );

    require(
        output.rtpSequenceNumber == 1 &&
        !output.discontinuity,
        "Existing block 1 unchanged"
    );

    require(
        queue.tryPush(makeBlock(5)) == Push::Accepted,
        "Accept block after overflow"
    );

    require(
        queue.waitPop(output, 100ms) == Pop::Data,
        "Pop block 2"
    );

    require(
        output.rtpSequenceNumber == 2 &&
        !output.discontinuity,
        "Existing block 2 unchanged"
    );

    require(
        queue.tryPush(makeBlock(6)) == Push::Accepted,
        "Accept subsequent block"
    );

    require(
        queue.waitPop(output, 100ms) == Pop::Data,
        "Pop block 5"
    );

    require(
        output.rtpSequenceNumber == 5 &&
        output.discontinuity,
        "First accepted block marked"
    );

    require(
        queue.waitPop(output, 100ms) == Pop::Data,
        "Pop block 6"
    );

    require(
        output.rtpSequenceNumber == 6 &&
        !output.discontinuity,
        "Discontinuity flag reset"
    );

    const auto stats = queue.statistics();

    require(stats.acceptedBlocks == 4, "Accepted count");
    require(stats.poppedBlocks == 4, "Popped count");
    require(stats.droppedBlocks == 2, "Dropped count");
    require(stats.droppedBytes == 376, "Dropped bytes");
    require(stats.peakBlocks == 2, "Peak block count");
    require(stats.peakBytes == 376, "Peak byte count");
    require(stats.queuedBytes == 0, "Empty byte count");

    std::cout << "[PASS] Overflow and discontinuity\n";
}

void testTimeout()
{
    BoundedTsQueue queue;
    TsDataBlock output;

    const auto start = std::chrono::steady_clock::now();

    require(
        queue.waitPop(output, 50ms) == Pop::Timeout,
        "Empty queue timeout"
    );

    const auto elapsed =
        std::chrono::steady_clock::now() - start;

    require(elapsed >= 50ms, "Timeout duration");

    std::cout << "[PASS] Consumer timeout\n";
}

void testGracefulClose()
{
    BoundedTsQueue queue;

    require(
        queue.tryPush(makeBlock(1)) == Push::Accepted,
        "Push before close"
    );

    require(
        queue.tryPush(makeBlock(2)) == Push::Accepted,
        "Second push before close"
    );

    queue.close();
    queue.close();

    require(
        queue.tryPush(makeBlock(3)) == Push::Closed,
        "Push after close"
    );

    TsDataBlock output;

    require(
        queue.waitPop(output, 100ms) == Pop::Data &&
        output.rtpSequenceNumber == 1,
        "Drain first block"
    );

    require(
        queue.waitPop(output, 100ms) == Pop::Data &&
        output.rtpSequenceNumber == 2,
        "Drain second block"
    );

    require(
        queue.waitPop(output, 100ms) == Pop::Closed,
        "Closed and empty queue"
    );

    const auto stats = queue.statistics();

    require(stats.closed, "Closed state");
    require(stats.poppedBlocks == 2, "Drained blocks");
    require(stats.rejectedClosedBlocks == 1, "Closed rejection");
    require(stats.discardedBlocks == 0, "No discarded blocks");

    std::cout << "[PASS] Graceful close and drain\n";
}

void testAbort()
{
    BoundedTsQueue queue;

    require(
        queue.tryPush(makeBlock(1)) == Push::Accepted,
        "First block before abort"
    );

    require(
        queue.tryPush(makeBlock(2)) == Push::Accepted,
        "Second block before abort"
    );

    queue.abort();
    queue.abort();

    TsDataBlock output;

    require(
        queue.waitPop(output, 100ms) == Pop::Closed,
        "Pop after abort"
    );

    const auto stats = queue.statistics();

    require(stats.closed, "Abort closed state");
    require(stats.discardedBlocks == 2, "Discarded blocks");
    require(stats.droppedBlocks == 2, "Abort dropped blocks");
    require(stats.droppedBytes == 376, "Abort dropped bytes");
    require(stats.queuedBlocks == 0, "Abort empty queue");
    require(stats.queuedBytes == 0, "Abort empty bytes");

    std::cout << "[PASS] Fatal abort and discard\n";
}

void testDataWakeup()
{
    BoundedTsQueue queue;

    std::promise<void> consumerReady;
    auto ready = consumerReady.get_future();

    auto consumer = std::async(
        std::launch::async,
        [&]()
        {
            TsDataBlock output;

            consumerReady.set_value();

            const auto result = queue.waitPop(output, 3s);

            return result == Pop::Data &&
                   output.rtpSequenceNumber == 42;
        }
    );

    ready.wait();

    require(
        consumer.wait_for(50ms) == std::future_status::timeout,
        "Consumer waits for data"
    );

    require(
        queue.tryPush(makeBlock(42)) == Push::Accepted,
        "Wakeup push"
    );

    require(
        consumer.wait_for(1s) == std::future_status::ready,
        "Consumer wakeup deadline"
    );

    require(consumer.get(), "Consumer received correct data");

    std::cout << "[PASS] Consumer data wakeup\n";
}

void testCloseWakeup()
{
    BoundedTsQueue queue;

    std::promise<void> consumerReady;
    auto ready = consumerReady.get_future();

    auto consumer = std::async(
        std::launch::async,
        [&]()
        {
            TsDataBlock output;

            consumerReady.set_value();

            return queue.waitPop(output, 3s);
        }
    );

    ready.wait();

    require(
        consumer.wait_for(50ms) == std::future_status::timeout,
        "Consumer waits before close"
    );

    queue.close();

    require(
        consumer.wait_for(1s) == std::future_status::ready,
        "Close wakeup deadline"
    );

    require(
        consumer.get() == Pop::Closed,
        "Consumer detects closed queue"
    );

    std::cout << "[PASS] Consumer close wakeup\n";
}

void testAbortWakeup()
{
    BoundedTsQueue queue;

    std::promise<void> consumerReady;
    auto ready = consumerReady.get_future();

    auto consumer = std::async(
        std::launch::async,
        [&]()
        {
            TsDataBlock output;

            consumerReady.set_value();

            return queue.waitPop(output, 3s);
        }
    );

    ready.wait();

    require(
        consumer.wait_for(50ms) == std::future_status::timeout,
        "Consumer waits before abort"
    );

    queue.abort();

    require(
        consumer.wait_for(1s) == std::future_status::ready,
        "Abort wakeup deadline"
    );

    require(
        consumer.get() == Pop::Closed,
        "Consumer detects aborted queue"
    );

    std::cout << "[PASS] Consumer abort wakeup\n";
}

void testConcurrentProducerConsumer()
{
    constexpr std::uint16_t totalBlocks = 10000;

    BoundedTsQueue queue(188 * 64, 64);

    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> dropped{0};

    std::thread consumer(
        [&]()
        {
            TsDataBlock output;

            std::uint16_t previousSequence = 0;
            bool firstBlock = true;

            while (true)
            {
                const auto result = queue.waitPop(output, 100ms);

                if (result == Pop::Closed)
                {
                    break;
                }

                if (result == Pop::Timeout)
                {
                    continue;
                }

                require(
                    output.size() == 188,
                    "Concurrent block size"
                );

                if (!firstBlock)
                {
                    require(
                        output.rtpSequenceNumber > previousSequence,
                        "Concurrent FIFO ordering"
                    );

                    if (
                        output.rtpSequenceNumber !=
                        previousSequence + 1
                    )
                    {
                        require(
                            output.discontinuity,
                            "Concurrent gap discontinuity"
                        );
                    }
                }

                previousSequence = output.rtpSequenceNumber;
                firstBlock = false;

                ++consumed;
            }
        }
    );

    std::thread producer(
        [&]()
        {
            for (
                std::uint16_t sequence = 0;
                sequence < totalBlocks;
                ++sequence
            )
            {
                const auto result =
                    queue.tryPush(makeBlock(sequence));

                if (result == Push::Accepted)
                {
                    ++accepted;
                }
                else if (result == Push::Full)
                {
                    ++dropped;
                }
                else
                {
                    require(false, "Unexpected producer result");
                }
            }

            queue.close();
        }
    );

    producer.join();
    consumer.join();

    const auto stats = queue.statistics();

    require(
        accepted.load() + dropped.load() == totalBlocks,
        "Concurrent total block accounting"
    );

    require(
        accepted.load() == consumed.load(),
        "All accepted blocks consumed"
    );

    require(
        stats.acceptedBlocks == accepted.load(),
        "Concurrent accepted statistics"
    );

    require(
        stats.poppedBlocks == consumed.load(),
        "Concurrent consumed statistics"
    );

    require(
        stats.droppedBlocks == dropped.load(),
        "Concurrent dropped statistics"
    );

    require(
        stats.droppedBytes == dropped.load() * 188,
        "Concurrent dropped byte accounting"
    );

    require(
        stats.peakBlocks <= 64,
        "Concurrent block capacity"
    );

    require(
        stats.peakBytes <= 188 * 64,
        "Concurrent byte capacity"
    );

    require(
        stats.queuedBlocks == 0 && stats.queuedBytes == 0,
        "Concurrent final queue state"
    );

    std::cout << "[PASS] Concurrent producer and consumer\n";

    std::cout
        << "       Accepted: " << accepted.load()
        << ", consumed: " << consumed.load()
        << ", dropped: " << dropped.load()
        << '\n';
}

} // namespace

int main()
{
    testInvalidConfiguration();
    testFifoAndMove();
    testByteLimit();
    testBlockLimit();
    testInvalidBlocks();
    testOverflowDiscontinuity();
    testTimeout();
    testGracefulClose();
    testAbort();
    testDataWakeup();
    testCloseWakeup();
    testAbortWakeup();
    testConcurrentProducerConsumer();

    std::cout << '\n';
    std::cout << "All BoundedTsQueue tests passed.\n";

    return EXIT_SUCCESS;
}
