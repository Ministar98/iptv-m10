#pragma once

#include "ControlCommand.hpp"
#include "ControlOperationResult.hpp"
#include "SessionController.hpp"

class ControlCommandExecutor
{
public:
    explicit ControlCommandExecutor(
        SessionController& controller
    );

    ControlCommandExecutor(
        const ControlCommandExecutor&
    ) = delete;

    ControlCommandExecutor& operator=(
        const ControlCommandExecutor&
    ) = delete;

    ControlOperationResult execute(
        const ControlCommand& command
    );

private:
    SessionController& controller_;

    static bool isChannelCommand(
        ControlCommandType type
    ) noexcept;

    static bool isKnownCommand(
        ControlCommandType type
    ) noexcept;

    bool dispatch(
        const ControlCommand& command
    );
};
