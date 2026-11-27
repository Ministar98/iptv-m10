#pragma once

#include "ControlCommand.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

enum class CommandSubmitStatus
{
    Accepted,
    Full,
    Closed,
    IdExhausted
};

struct CommandSubmitResult
{
    CommandSubmitStatus status =
        CommandSubmitStatus::Closed;

    std::uint64_t operationId = 0;

    bool accepted() const noexcept
    {
        return status ==
            CommandSubmitStatus::Accepted;
    }
};

enum class CommandPopStatus
{
    Command,
    Timeout,
    Closed
};

struct CommandPopResult
{
    CommandPopStatus status =
        CommandPopStatus::Timeout;

    std::optional<ControlCommand> command;
};

class ControlCommandQueue
{
public:
    explicit ControlCommandQueue(
        std::size_t capacity = 128
    );

    ControlCommandQueue(
        const ControlCommandQueue&
    ) = delete;

    ControlCommandQueue& operator=(
        const ControlCommandQueue&
    ) = delete;

    CommandSubmitResult submit(
        ControlCommand command
    );

    CommandPopResult waitPop(
        std::chrono::milliseconds timeout
    );

    std::optional<ControlCommand> tryPop();

    void close();

    std::size_t size() const;

    std::size_t capacity() const noexcept;

    bool isClosed() const;

private:
    const std::size_t capacity_;

    mutable std::mutex mutex_;

    std::condition_variable condition_;

    std::deque<ControlCommand> commands_;

    std::uint64_t lastOperationId_ = 0;

    bool closed_ = false;
};
