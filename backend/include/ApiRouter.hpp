#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class ApiRoute
{
    Unknown,
    Services,
    Epg,
    Session,
    SelectChannel,
    PlaybackStatus,
    Pause,
    Resume,
    Seek,
    GoLive,
    PsiDiagnostics,
    Health,
    Events,
    AcknowledgeSeek,
    ReportPlaybackPosition
};

enum class ApiRouteStatus
{
    Matched,
    NotFound,
    MethodNotAllowed,
    InvalidTarget
};

struct ApiRouteResult
{
    ApiRoute route = ApiRoute::Unknown;

    ApiRouteStatus status = ApiRouteStatus::NotFound;

    std::optional<std::uint16_t> serviceId;

    std::string query;
    std::string allowedMethod;

    bool matched() const noexcept
    {
        return status == ApiRouteStatus::Matched;
    }
};

class ApiRouter
{
public:
    static ApiRouteResult resolve(
        std::string_view method,
        std::string_view target
    );
};
