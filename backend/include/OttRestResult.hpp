#pragma once

#include <string>

enum class OttRestStatus
{
    Success,
    InvalidRequest,
    ConnectionError,
    Timeout,
    HttpError,
    InvalidResponse,
    InternalError
};

inline constexpr const char* ottRestStatusToString(
    OttRestStatus status
) noexcept
{
    switch (status)
    {
        case OttRestStatus::Success:
            return "SUCCESS";

        case OttRestStatus::InvalidRequest:
            return "INVALID_REQUEST";

        case OttRestStatus::ConnectionError:
            return "CONNECTION_ERROR";

        case OttRestStatus::Timeout:
            return "TIMEOUT";

        case OttRestStatus::HttpError:
            return "HTTP_ERROR";

        case OttRestStatus::InvalidResponse:
            return "INVALID_RESPONSE";

        case OttRestStatus::InternalError:
            return "INTERNAL_ERROR";
    }

    return "UNKNOWN";
}

template<typename T>
struct OttRestResult
{
    OttRestStatus status = OttRestStatus::InternalError;

    long httpStatus = 0;

    T data{};

    std::string errorMessage;

    bool success() const noexcept
    {
        return status == OttRestStatus::Success;
    }
};
