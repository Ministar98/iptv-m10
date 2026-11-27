#include "ControlCommandQueue.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

ControlCommand makeCommand(
    ControlCommandType type,
    std::uint64_t generation = 1
)
{
    ControlCommand command;

    command.type = type;
    command.generationId = generation;
    command.requestId = "test-request";

    return command;
}

void testInvalidCapacity()
{
    bool rejected = false;

    try
    {
        ControlCommandQueue queue(0);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    require(
        rejected,
        "Zero capacity accepted"
    );

    pass("Invalid capacity rejected");
}

void testInitialState()
{
    ControlCommandQueue queue;

    require(
        queue.capacity() == 128 &&
        queue.size() == 0 &&
        !queue.isClosed() &&
        !queue.tryPop().has_value(),
        "Invalid initial queue state"
    );

    pass("Initial queue state");
}

void testFifo()
{
    ControlCommandQueue queue(3);

    const auto first = queue.submit(
        makeCommand(ControlCommandType::Pause)
    );

    const auto second = queue.submit(
        makeCommand(ControlCommandType::Resume)
    );

    require(
        first.accepted() &&
        second.accepted() &&
        first.operationId == 1 &&
        second.operationId == 2 &&
        queue.size() == 2,
        "Invalid command submission"
    );

    pass("Unique sequential operation IDs");

    const auto a = queue.tryPop();
    const auto b = queue.tryPop();

    require(
        a.has_value() &&
        b.has_value() &&
        a->type == ControlCommandType::Pause &&
        b->type == ControlCommandType::Resume &&
        a->operationId == first.operationId &&
        b->operationId == second.operationId &&
        queue.size() == 0,
        "FIFO ordering failed"
    );

    pass("FIFO command ordering");
}

void testCommandPayload()
{
    ControlCommandQueue queue;

    auto command = makeCommand(
        ControlCommandType::Seek,
        7
    );

    command.seekPositionMs = 12500;
    command.operationId = 999;

    const auto submitted = queue.submit(command);

    const auto result = queue.tryPop();

    require(
        submitted.accepted() &&
        result.has_value() &&
        result->generationId == 7 &&
        result->seekPositionMs == 12500 &&
        result->requestId == "test-request" &&
        result->operationId == 1,
        "Command payload was not preserved"
    );

    pass("Command payload and assigned operation ID");
}

void testCapacity()
{
    ControlCommandQueue queue(2);

    require(
        queue.submit(
            makeCommand(ControlCommandType::Pause)
        ).accepted(),
        "First submission failed"
    );

    require(
        queue.submit(
            makeCommand(ControlCommandType::Resume)
        ).accepted(),
        "Second submission failed"
    );

    const auto rejected = queue.submit(
        makeCommand(ControlCommandType::GoLive)
    );

    require(
        rejected.status ==
            CommandSubmitStatus::Full &&
        rejected.operationId == 0 &&
        queue.size() == 2,
        "Queue capacity exceeded"
    );

    pass("Full queue rejects new commands");

    require(
        queue.tryPop().has_value(),
        "Failed to remove command"
    );

    const auto accepted = queue.submit(
        makeCommand(ControlCommandType::GoLive)
    );

    require(
        accepted.accepted() &&
        accepted.operationId == 3 &&
        queue.size() == 2,
        "Queue did not recover after pop"
    );

    pass("Queue capacity recovery");
}

void testTimeout()
{
    ControlCommandQueue queue;

    const auto result = queue.waitPop(30ms);

    require(
        result.status == CommandPopStatus::Timeout &&
        !result.command.has_value(),
        "Invalid timeout result"
    );

    pass("Empty queue timeout");
}

void testWakeup()
{
    ControlCommandQueue queue;

    std::thread producer(
        [&queue]()
        {
            std::this_thread::sleep_for(20ms);

            queue.submit(
                makeCommand(
                    ControlCommandType::NextChannel
                )
            );
        }
    );

    const auto result = queue.waitPop(1000ms);

    producer.join();

    require(
        result.status == CommandPopStatus::Command &&
        result.command.has_value() &&
        result.command->type ==
            ControlCommandType::NextChannel,
        "Waiting consumer was not notified"
    );

    pass("Consumer wakeup after submission");
}

void testClose()
{
    ControlCommandQueue queue;

    queue.submit(
        makeCommand(ControlCommandType::Pause)
    );

    queue.submit(
        makeCommand(ControlCommandType::Resume)
    );

    queue.close();
    queue.close();

    const auto rejected = queue.submit(
        makeCommand(ControlCommandType::GoLive)
    );

    require(
        queue.isClosed() &&
        rejected.status ==
            CommandSubmitStatus::Closed,
        "Closed queue accepted command"
    );

    pass("Closed queue rejects submissions");

    const auto first = queue.waitPop(100ms);
    const auto second = queue.waitPop(100ms);
    const auto third = queue.waitPop(100ms);

    require(
        first.status == CommandPopStatus::Command &&
        second.status == CommandPopStatus::Command &&
        third.status == CommandPopStatus::Closed &&
        queue.size() == 0,
        "Queue did not drain correctly"
    );

    pass("Graceful queue draining");
}

void testCloseWakeup()
{
    ControlCommandQueue queue;

    std::thread closer(
        [&queue]()
        {
            std::this_thread::sleep_for(20ms);

            queue.close();
        }
    );

    const auto result = queue.waitPop(1000ms);

    closer.join();

    require(
        result.status == CommandPopStatus::Closed &&
        !result.command.has_value(),
        "Close did not wake waiting consumer"
    );

    pass("Close wakes waiting consumer");
}

void testConcurrentProducers()
{
    constexpr std::size_t producerCount = 4;

    constexpr std::size_t commandsPerProducer = 500;

    constexpr std::size_t total =
        producerCount * commandsPerProducer;

    ControlCommandQueue queue(32);

    std::atomic<bool> producerFailed{false};

    std::vector<std::thread> producers;

    std::vector<std::uint64_t> receivedIds;

    receivedIds.reserve(total);

    std::thread consumer(
        [&]()
        {
            while (true)
            {
                const auto result =
                    queue.waitPop(100ms);

                if (result.status ==
                    CommandPopStatus::Closed)
                {
                    break;
                }

                if (result.command)
                {
                    receivedIds.push_back(
                        result.command->operationId
                    );
                }
            }
        }
    );

    for (std::size_t i = 0;
         i < producerCount;
         ++i)
    {
        producers.emplace_back(
            [&]()
            {
                for (std::size_t j = 0;
                     j < commandsPerProducer;
                     ++j)
                {
                    while (true)
                    {
                        const auto result =
                            queue.submit(
                                makeCommand(
                                    ControlCommandType::Seek
                                )
                            );

                        if (result.accepted())
                        {
                            break;
                        }

                        if (result.status !=
                            CommandSubmitStatus::Full)
                        {
                            producerFailed.store(true);

                            return;
                        }

                        std::this_thread::yield();
                    }
                }
            }
        );
    }

    for (auto& producer : producers)
    {
        producer.join();
    }

    queue.close();

    consumer.join();

    require(
        !producerFailed.load() &&
        receivedIds.size() == total,
        "Concurrent commands were lost"
    );

    for (std::size_t i = 0;
         i < receivedIds.size();
         ++i)
    {
        require(
            receivedIds[i] == i + 1,
            "Duplicate or unordered operation ID"
        );
    }

    pass("Concurrent producers: 2000 commands");
}

} // namespace

int main()
{
    try
    {
        testInvalidCapacity();
        testInitialState();
        testFifo();
        testCommandPayload();
        testCapacity();
        testTimeout();
        testWakeup();
        testClose();
        testCloseWakeup();
        testConcurrentProducers();

        std::cout
            << "\nAll Control Command Queue tests passed.\n";

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
