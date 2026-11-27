#include "ApiStateCache.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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

void pass(const char* message)
{
    std::cout << "[PASS] " << message << '\n';
}

SessionSnapshot makeSession(std::uint64_t generation)
{
    SessionSnapshot session;

    session.state = SessionState::Running;
    session.generationId = generation;

    session.playback.generationId = generation;
    session.playback.state = PlaybackState::Live;
    session.playback.bufferAvailable = true;
    session.playback.livePositionMs = 10000;
    session.playback.playbackPositionMs = 10000;

    TvService service;

    service.serviceId = 1;
    service.channelNumber = 1;
    service.name = "Channel 1";

    session.activeService = service;

    return session;
}

void testBasicOperations()
{
    ApiStateCache cache;

    require(!cache.initialized(),
            "Initial cache state");

    require(!cache.snapshot().initialized,
            "Initial snapshot state");

    pass("Initial cache state");

    const auto session = makeSession(1);

    ActiveSessionStatistics statistics;

    statistics.producer.receivedDatagrams = 100;
    statistics.consumer.buffer.segmentCount = 5;

    require(cache.publish(session, statistics),
            "Initial publication");

    require(cache.initialized(),
            "Cache initialized");

    pass("Initial state publication");

    auto snapshot = cache.snapshot();

    require(snapshot.revision == 1,
            "Initial revision");

    require(snapshot.session.generationId == 1,
            "Initial generation");

    require(snapshot.session.activeService.has_value(),
            "Active service available");

    require(snapshot.statistics.has_value(),
            "Statistics available");

    require(
        snapshot.statistics->producer.receivedDatagrams == 100,
        "Producer statistics"
    );

    require(
        snapshot.statistics->consumer.buffer.segmentCount == 5,
        "Buffer statistics"
    );

    pass("Snapshot content");

    auto updated = session;

    updated.playback.state = PlaybackState::Paused;

    require(cache.publish(updated, statistics),
            "Same generation update");

    snapshot = cache.snapshot();

    require(snapshot.revision == 2,
            "Revision increment");

    require(
        snapshot.session.playback.state == PlaybackState::Paused,
        "Updated playback state"
    );

    pass("Same generation update");

    require(cache.publish(makeSession(2), statistics),
            "New generation publication");

    require(cache.snapshot().session.generationId == 2,
            "New generation");

    pass("New generation publication");

    require(!cache.publish(session, statistics),
            "Stale generation rejection");

    require(cache.snapshot().session.generationId == 2,
            "Generation preserved");

    require(cache.snapshot().revision == 3,
            "Revision preserved");

    pass("Stale generation rejected");

    auto detached = cache.snapshot();

    detached.session.generationId = 999;

    require(cache.snapshot().session.generationId == 2,
            "Snapshot independence");

    pass("Independent snapshot copy");

    SessionSnapshot idle;

    idle.generationId = 2;
    idle.playback.generationId = 2;

    require(cache.publish(idle, std::nullopt),
            "Idle publication");

    snapshot = cache.snapshot();

    require(snapshot.session.state == SessionState::Idle,
            "Idle state");

    require(!snapshot.statistics.has_value(),
            "Statistics cleared");

    require(!snapshot.session.activeService.has_value(),
            "Active service cleared");

    pass("Idle state without statistics");
}

void testConcurrentAccess()
{
    ApiStateCache cache;

    require(cache.publish(makeSession(1), std::nullopt),
            "Concurrent test initialization");

    constexpr std::uint64_t publications = 10000;

    std::atomic<bool> finished{false};
    std::atomic<bool> valid{true};

    std::vector<std::thread> readers;

    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back([&]()
        {
            std::uint64_t previousRevision = 0;

            do
            {
                const auto snapshot = cache.snapshot();

                if (!snapshot.initialized ||
                    snapshot.revision < previousRevision ||
                    snapshot.session.generationId !=
                        snapshot.session.playback.generationId)
                {
                    valid.store(false);
                }

                previousRevision = snapshot.revision;

            } while (!finished.load());
        });
    }

    std::thread writer([&]()
    {
        for (std::uint64_t i = 2; i <= publications; ++i)
        {
            if (!cache.publish(makeSession(i), std::nullopt))
            {
                valid.store(false);
                break;
            }
        }

        finished.store(true);
    });

    writer.join();

    for (auto& reader : readers)
    {
        reader.join();
    }

    require(valid.load(),
            "Concurrent snapshot consistency");

    pass("Concurrent readers and writer");

    const auto snapshot = cache.snapshot();

    require(snapshot.revision == publications,
            "Final revision");

    require(snapshot.session.generationId == publications,
            "Final generation");

    pass("All concurrent publications preserved");
}

} // namespace

int main()
{
    testBasicOperations();

    testConcurrentAccess();

    std::cout << "\nAll API State Cache tests passed.\n";

    return EXIT_SUCCESS;
}
