#include "EventQueue.hpp"
#include "ApiOperationJson.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

void testInvalidCapacity()
{
    bool rejected = false;

    try
    {
        EventQueue queue(0);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    require(rejected, "Zero capacity accepted");

    pass("Invalid capacity rejected");
}

void testInitialState()
{
    EventQueue queue;

    const auto stats = queue.statistics();

    require(
        queue.capacity() == 256 &&
        stats.queued == 0 &&
        stats.published == 0 &&
        stats.dropped == 0 &&
        !stats.closed &&
        !queue.tryPop(),
        "Invalid initial state"
    );

    pass("Initial event queue state");
}

void testFifo()
{
    EventQueue queue(3);

    require(
        queue.publish({{"id", 1}}) &&
        queue.publish({{"id", 2}}) &&
        queue.publish({{"id", 3}}),
        "Event publication failed"
    );

    for (int i = 1; i <= 3; ++i)
    {
        const auto event = queue.tryPop();

        require(
            event && (*event)["id"] == i,
            "Invalid event ordering"
        );
    }

    const auto stats = queue.statistics();

    require(
        stats.published == 3 &&
        stats.consumed == 3 &&
        stats.queued == 0 &&
        stats.peakQueued == 3 &&
        stats.dropped == 0,
        "Invalid FIFO statistics"
    );

    pass("FIFO event ordering");

    pass("Event queue statistics");
}

void testOverflow()
{
    EventQueue queue(2);

    queue.publish({{"id", 1}});
    queue.publish({{"id", 2}});
    queue.publish({{"id", 3}});

    const auto stats = queue.statistics();

    require(
        stats.queued == 2 &&
        stats.dropped == 1 &&
        stats.published == 3 &&
        stats.peakQueued == 2,
        "Invalid overflow handling"
    );

    pass("Queue overflow detected");

    const auto first = queue.tryPop();
    const auto second = queue.tryPop();

    require(
        first && second &&
        (*first)["id"] == 2 &&
        (*second)["id"] == 3,
        "Newest events were not preserved"
    );

    pass("Overflow preserves newest events");
}

void testClose()
{
    EventQueue queue;

    queue.publish({{"id", 1}});

    queue.close();
    queue.close();

    require(
        !queue.publish({{"id", 2}}) &&
        queue.statistics().rejectedClosed == 1,
        "Closed queue accepted event"
    );

    pass("Closed queue rejects publications");

    const auto event = queue.tryPop();

    require(
        event &&
        (*event)["id"] == 1 &&
        !queue.tryPop() &&
        queue.statistics().closed,
        "Closed queue failed to drain"
    );

    pass("Closed queue preserves pending events");
}

void testOperationResult()
{
    ControlOperationResult result;

    result.command.type =
        ControlCommandType::SelectService;

    result.command.requestId = "request-42";
    result.command.operationId = 42;
    result.command.generationId = 5;
    result.command.serviceId = 2;

    result.status =
        ControlOperationStatus::Completed;

    result.session.state = SessionState::Running;
    result.session.generationId = 6;

    TvService service;

    service.serviceId = 2;
    service.channelNumber = 2;
    service.name = "Channel 2";

    result.session.activeService = service;
    result.session.playback.generationId = 6;
    result.session.playback.state =
        PlaybackState::Buffering;

    auto event = ApiJson::operationResult(result);

    require(
        event["type"] == "OPERATION_RESULT" &&
        event["requestId"] == "request-42" &&
        event["operationId"] == 42 &&
        event["generationId"] == 6 &&
        event["sessionId"] == 6 &&
        event["payload"]["requestedGenerationId"] == 5 &&
        event["payload"]["command"] == "SELECT_SERVICE" &&
        event["payload"]["status"] == "COMPLETED" &&
        event["payload"]["session"]["serviceId"] == 2 &&
        !event["payload"].contains("error"),
        "Invalid completed operation event"
    );

    pass("Completed operation serialization");

    require(
        event["payload"]["session"]["playback"]["state"] ==
            "BUFFERING",
        "Invalid playback state"
    );

    pass("Operation completion preserves buffering state");

    result.status =
        ControlOperationStatus::Rejected;

    result.errorCode = "STALE_GENERATION";
    result.errorMessage = "Outdated command";

    event = ApiJson::operationResult(result);

    require(
        event["payload"]["status"] == "REJECTED" &&
        event["payload"]["error"]["code"] ==
            "STALE_GENERATION" &&
        event["payload"]["error"]["message"] ==
            "Outdated command",
        "Invalid rejected operation event"
    );

    pass("Rejected operation serialization");

    result.status =
        ControlOperationStatus::Failed;

    result.errorCode = "SESSION_ERROR";
    result.errorMessage = "Headend failed";

    event = ApiJson::operationResult(result);

    require(
        event["payload"]["status"] == "FAILED" &&
        event["payload"]["error"]["code"] ==
            "SESSION_ERROR",
        "Invalid failed operation event"
    );

    pass("Failed operation serialization");

    EventQueue queue;

    queue.publish(event);

    const auto received = queue.tryPop();

    require(
        received && *received == event,
        "Operation event was modified"
    );

    pass("Operation result queue integration");
}

void testConcurrentAccess()
{
    constexpr std::size_t producerCount = 4;
    constexpr std::size_t eventsPerProducer = 500;

    constexpr std::size_t total =
        producerCount * eventsPerProducer;

    EventQueue queue(total);

    std::atomic<bool> producersFinished{false};

    std::vector<std::thread> producers;

    std::vector<bool> received(total, false);

    std::size_t receivedCount = 0;

    bool invalidEvent = false;

    std::thread consumer(
        [&]()
        {
            while (true)
            {
                const auto event = queue.tryPop();

                if (event)
                {
                    const auto id =
                        (*event)["id"].get<std::size_t>();

                    if (id >= total || received[id])
                    {
                        invalidEvent = true;
                    }
                    else
                    {
                        received[id] = true;
                        ++receivedCount;
                    }

                    continue;
                }

                if (producersFinished.load() &&
                    queue.statistics().queued == 0)
                {
                    break;
                }

                std::this_thread::yield();
            }
        }
    );

    for (std::size_t i = 0;
         i < producerCount;
         ++i)
    {
        producers.emplace_back(
            [&, i]()
            {
                for (std::size_t j = 0;
                     j < eventsPerProducer;
                     ++j)
                {
                    queue.publish({
                        {
                            "id",
                            i * eventsPerProducer + j
                        }
                    });
                }
            }
        );
    }

    for (auto& producer : producers)
    {
        producer.join();
    }

    producersFinished.store(true);

    consumer.join();

    const auto stats = queue.statistics();

    require(
        !invalidEvent &&
        receivedCount == total &&
        stats.published == total &&
        stats.consumed == total &&
        stats.dropped == 0 &&
        stats.queued == 0,
        "Concurrent event delivery failed"
    );

    pass("Concurrent event delivery: 2000 events");
}

}

int main()
{
    try
    {
        testInvalidCapacity();
        testInitialState();
        testFifo();
        testOverflow();
        testClose();
        testOperationResult();
        testConcurrentAccess();

        std::cout
            << "\nAll Event Queue tests passed.\n";

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
