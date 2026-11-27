#include "ControlWorker.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
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
    ControlCommandType type
)
{
    ControlCommand command;

    command.type = type;
    command.generationId = 0;
    command.requestId = "worker-test";

    return command;
}

void testInvalidHandler()
{
    const ServiceManager services;

    SessionController controller(
        services,
        HlsConfig{},
        BufferConfig{}
    );

    ControlCommandExecutor executor(controller);

    ControlCommandQueue queue;

    bool rejected = false;

    try
    {
        ControlWorker worker(
            queue,
            executor,
            {}
        );
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    require(
        rejected,
        "Empty result handler accepted"
    );

    pass("Empty result handler rejected");
}

void testWorker()
{
    const ServiceManager services;

    SessionController controller(
        services,
        HlsConfig{},
        BufferConfig{}
    );

    ControlCommandExecutor executor(controller);

    ControlCommandQueue queue(128);

    std::mutex resultMutex;

    std::condition_variable resultCondition;

    std::vector<ControlOperationResult> results;

    ControlWorker worker(
        queue,
        executor,
        [&](const ControlOperationResult& result)
        {
            {
                std::lock_guard<std::mutex> lock(
                    resultMutex
                );

                results.push_back(result);
            }

            resultCondition.notify_all();
        }
    );

    require(
        !worker.isRunning() &&
        worker.statistics().processed == 0,
        "Invalid initial worker state"
    );

    pass("Initial worker state");

    require(
        worker.start() &&
        worker.isRunning(),
        "Worker startup failed"
    );

    pass("Worker startup");

    require(
        !worker.start(),
        "Duplicate worker startup accepted"
    );

    pass("Duplicate startup rejected");

    const auto first = queue.submit(
        makeCommand(ControlCommandType::Pause)
    );

    require(
        first.accepted(),
        "Command submission failed"
    );

    {
        std::unique_lock<std::mutex> lock(resultMutex);

        require(
            resultCondition.wait_for(
                lock,
                2s,
                [&]()
                {
                    return !results.empty();
                }
            ),
            "Asynchronous result timeout"
        );

        require(
            results.front().command.operationId ==
                first.operationId &&
            results.front().command.requestId ==
                "worker-test" &&
            results.front().errorCode ==
                "SESSION_NOT_RUNNING",
            "Invalid asynchronous result"
        );
    }

    pass("Asynchronous command execution");

    pass("Operation result correlation");

    constexpr std::size_t additionalCommands = 100;

    for (std::size_t i = 0;
         i < additionalCommands;
         ++i)
    {
        require(
            queue.submit(
                makeCommand(
                    ControlCommandType::Resume
                )
            ).accepted(),
            "Batch submission failed"
        );
    }

    worker.stop();

    require(
        !worker.isRunning() &&
        queue.isClosed() &&
        queue.size() == 0,
        "Worker shutdown failed"
    );

    pass("Graceful worker shutdown");

    require(
        results.size() == additionalCommands + 1,
        "Pending operation results were lost"
    );

    pass("All pending commands drained");

    for (std::size_t i = 0;
         i < results.size();
         ++i)
    {
        require(
            results[i].command.operationId == i + 1 &&
            results[i].status ==
                ControlOperationStatus::Rejected,
            "Invalid result ordering"
        );
    }

    pass("FIFO operation result ordering");

    const auto statistics = worker.statistics();

    require(
        statistics.processed == 101 &&
        statistics.rejected == 101 &&
        statistics.completed == 0 &&
        statistics.failed == 0 &&
        statistics.callbackErrors == 0,
        "Invalid worker statistics"
    );

    pass("Worker statistics");

    require(
        !queue.submit(
            makeCommand(ControlCommandType::Pause)
        ).accepted(),
        "Stopped worker queue accepted command"
    );

    pass("Submissions rejected after shutdown");

    worker.stop();

    require(
        !worker.start(),
        "Stopped worker restarted"
    );

    pass("Idempotent shutdown and restart protection");
}

void testCallbackFailure()
{
    const ServiceManager services;

    SessionController controller(
        services,
        HlsConfig{},
        BufferConfig{}
    );

    ControlCommandExecutor executor(controller);

    ControlCommandQueue queue;

    std::size_t callbackCount = 0;

    ControlWorker worker(
        queue,
        executor,
        [&](const ControlOperationResult&)
        {
            ++callbackCount;

            if (callbackCount == 1)
            {
                throw std::runtime_error(
                    "Simulated callback failure"
                );
            }
        }
    );

    require(
        worker.start(),
        "Worker startup failed"
    );

    require(
        queue.submit(
            makeCommand(ControlCommandType::Pause)
        ).accepted() &&
        queue.submit(
            makeCommand(ControlCommandType::Resume)
        ).accepted(),
        "Command submission failed"
    );

    worker.stop();

    const auto statistics = worker.statistics();

    require(
        callbackCount == 2 &&
        statistics.processed == 2 &&
        statistics.rejected == 2 &&
        statistics.callbackErrors == 1,
        "Callback failure interrupted worker"
    );

    pass("Worker survives result handler failure");
}

void testClosedQueue()
{
    const ServiceManager services;

    SessionController controller(
        services,
        HlsConfig{},
        BufferConfig{}
    );

    ControlCommandExecutor executor(controller);

    ControlCommandQueue queue;

    queue.close();

    ControlWorker worker(
        queue,
        executor,
        [](const ControlOperationResult&)
        {
        }
    );

    require(
        !worker.start() &&
        !worker.isRunning(),
        "Worker started with closed queue"
    );

    pass("Closed queue prevents startup");
}

} // namespace

int main()
{
    try
    {
        testInvalidHandler();
        testWorker();
        testCallbackFailure();
        testClosedQueue();

        std::cout
            << "\nAll Control Worker tests passed.\n";

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
