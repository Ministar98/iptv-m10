#pragma once

#include "ControlCommand.hpp"
#include "SessionSnapshot.hpp"

#include <string>

enum class ControlOperationStatus
{
    Completed,
    Rejected,
    Failed
};

inline constexpr const char*
controlOperationStatusToString(
    ControlOperationStatus status
) noexcept
{
    switch (status)
    {
        case ControlOperationStatus::Completed:
            return "COMPLETED";

        case ControlOperationStatus::Rejected:
            return "REJECTED";

        case ControlOperationStatus::Failed:
            return "FAILED";
    }

    return "UNKNOWN";
}

struct ControlOperationResult
{
    ControlCommand command;

    ControlOperationStatus status =
        ControlOperationStatus::Failed;

    SessionSnapshot session;

    std::string errorCode;

    std::string errorMessage;

    bool success() const noexcept
    {
        return status ==
            ControlOperationStatus::Completed;
    }
};
