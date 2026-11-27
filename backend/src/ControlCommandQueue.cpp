#include "ControlCommandQueue.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

ControlCommandQueue::ControlCommandQueue(
    std::size_t capacity
)
    : capacity_(capacity)
{
    if (capacity_ == 0)
    {
        throw std::invalid_argument(
            "Command queue capacity must be positive"
        );
    }
}

CommandSubmitResult ControlCommandQueue::submit(
    ControlCommand command
)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (closed_)
    {
        return {
            CommandSubmitStatus::Closed,
            0
        };
    }

    if (commands_.size() >= capacity_)
    {
        return {
            CommandSubmitStatus::Full,
            0
        };
    }

    if (lastOperationId_ ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return {
            CommandSubmitStatus::IdExhausted,
            0
        };
    }

    const auto operationId =
        lastOperationId_ + 1;

    command.operationId = operationId;

    commands_.push_back(std::move(command));

    lastOperationId_ = operationId;

    lock.unlock();

    condition_.notify_one();

    return {
        CommandSubmitStatus::Accepted,
        operationId
    };
}

CommandPopResult ControlCommandQueue::waitPop(
    std::chrono::milliseconds timeout
)
{
    std::unique_lock<std::mutex> lock(mutex_);

    const bool ready = condition_.wait_for(
        lock,
        timeout,
        [this]()
        {
            return closed_ || !commands_.empty();
        }
    );

    if (!ready)
    {
        return {
            CommandPopStatus::Timeout,
            std::nullopt
        };
    }

    if (commands_.empty())
    {
        return {
            CommandPopStatus::Closed,
            std::nullopt
        };
    }

    ControlCommand command =
        std::move(commands_.front());

    commands_.pop_front();

    return {
        CommandPopStatus::Command,
        std::move(command)
    };
}

std::optional<ControlCommand>
ControlCommandQueue::tryPop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (commands_.empty())
    {
        return std::nullopt;
    }

    ControlCommand command =
        std::move(commands_.front());

    commands_.pop_front();

    return command;
}

void ControlCommandQueue::close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        closed_ = true;
    }

    condition_.notify_all();
}

std::size_t ControlCommandQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return commands_.size();
}

std::size_t
ControlCommandQueue::capacity() const noexcept
{
    return capacity_;
}

bool ControlCommandQueue::isClosed() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return closed_;
}
