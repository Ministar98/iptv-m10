#include "ControlWorker.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

ControlWorker::ControlWorker(
    ControlCommandQueue& queue,
    ControlCommandExecutor& executor,
    ResultHandler resultHandler
)
    : queue_(queue),
      executor_(executor),
      resultHandler_(std::move(resultHandler))
{
    if (!resultHandler_)
    {
        throw std::invalid_argument(
            "Control worker requires a result handler"
        );
    }
}

ControlWorker::~ControlWorker()
{
    stop();
}

bool ControlWorker::start()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (started_ || queue_.isClosed())
    {
        return false;
    }

    running_.store(true);

    try
    {
        workerThread_ = std::thread(
            &ControlWorker::run,
            this
        );

        started_ = true;

        return true;
    }
    catch (const std::exception& error)
    {
        running_.store(false);

        std::cerr
            << "[ERROR] Control worker startup failed: "
            << error.what()
            << '\n';

        return false;
    }
}

void ControlWorker::stop()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    queue_.close();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    running_.store(false);
}

bool ControlWorker::isRunning() const noexcept
{
    return running_.load();
}

ControlWorkerStatistics
ControlWorker::statistics() const
{
    std::lock_guard<std::mutex> lock(statisticsMutex_);

    return statistics_;
}

void ControlWorker::run()
{
    using namespace std::chrono_literals;

    while (true)
    {
        const auto result = queue_.waitPop(100ms);

        if (result.status == CommandPopStatus::Closed)
        {
            break;
        }

        if (result.status == CommandPopStatus::Timeout)
        {
            continue;
        }

        if (result.command)
        {
            process(*result.command);
        }
    }

    running_.store(false);
}

void ControlWorker::process(
    const ControlCommand& command
)
{
    ControlOperationResult result;

    try
    {
        result = executor_.execute(command);
    }
    catch (const std::exception& error)
    {
        result.command = command;

        result.status =
            ControlOperationStatus::Failed;

        result.errorCode = "INTERNAL_ERROR";

        result.errorMessage = error.what();
    }
    catch (...)
    {
        result.command = command;

        result.status =
            ControlOperationStatus::Failed;

        result.errorCode = "INTERNAL_ERROR";

        result.errorMessage =
            "Unknown control worker error";
    }

    recordResult(result);

    try
    {
        resultHandler_(result);
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(
            statisticsMutex_
        );

        ++statistics_.callbackErrors;
    }
}

void ControlWorker::recordResult(
    const ControlOperationResult& result
)
{
    std::lock_guard<std::mutex> lock(statisticsMutex_);

    ++statistics_.processed;

    switch (result.status)
    {
        case ControlOperationStatus::Completed:
            ++statistics_.completed;
            break;

        case ControlOperationStatus::Rejected:
            ++statistics_.rejected;
            break;

        case ControlOperationStatus::Failed:
            ++statistics_.failed;
            break;
    }
}
