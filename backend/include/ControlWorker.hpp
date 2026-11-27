#pragma once

#include "ControlCommandQueue.hpp"
#include "ControlCommandExecutor.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

struct ControlWorkerStatistics
{
    std::uint64_t processed = 0;
    std::uint64_t completed = 0;
    std::uint64_t rejected = 0;
    std::uint64_t failed = 0;
    std::uint64_t callbackErrors = 0;
};

class ControlWorker
{
public:
    using ResultHandler =
        std::function<void(const ControlOperationResult&)>;

    ControlWorker(
        ControlCommandQueue& queue,
        ControlCommandExecutor& executor,
        ResultHandler resultHandler
    );

    ~ControlWorker();

    ControlWorker(const ControlWorker&) = delete;

    ControlWorker& operator=(
        const ControlWorker&
    ) = delete;

    bool start();

    void stop();

    bool isRunning() const noexcept;

    ControlWorkerStatistics statistics() const;

private:
    ControlCommandQueue& queue_;

    ControlCommandExecutor& executor_;

    const ResultHandler resultHandler_;

    std::thread workerThread_;

    mutable std::mutex lifecycleMutex_;

    mutable std::mutex statisticsMutex_;

    std::atomic<bool> running_{false};

    bool started_ = false;

    ControlWorkerStatistics statistics_{};

    void run();

    void process(const ControlCommand& command);

    void recordResult(
        const ControlOperationResult& result
    );
};
