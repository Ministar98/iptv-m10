#include "ApiRouter.hpp"

#include <charconv>
#include <limits>
#include <system_error>

namespace
{

struct RouteEntry
{
    std::string_view path;
    std::string_view method;
    ApiRoute route;
};

constexpr RouteEntry routes[] =
{
    {"/api/v1/services", "GET", ApiRoute::Services},

    {"/api/v1/session", "GET", ApiRoute::Session},

    {"/api/v1/session/channel", "POST",
        ApiRoute::SelectChannel},

    {"/api/v1/playback/status", "GET",
        ApiRoute::PlaybackStatus},

    {"/api/v1/playback/pause", "POST", ApiRoute::Pause},

    {"/api/v1/playback/resume", "POST", ApiRoute::Resume},

    {"/api/v1/playback/seek", "POST", ApiRoute::Seek},

    {"/api/v1/playback/live", "POST", ApiRoute::GoLive},

    {"/api/v1/playback/ack", "POST",
        ApiRoute::AcknowledgeSeek},

    {"/api/v1/playback/position", "POST",
        ApiRoute::ReportPlaybackPosition},

    {"/api/v1/diagnostics/psi", "GET",
        ApiRoute::PsiDiagnostics},

    {"/api/v1/health", "GET", ApiRoute::Health},

    {"/api/v1/events", "GET", ApiRoute::Events}
};

bool validTarget(std::string_view target)
{
    if (target.empty() ||
        target.size() > 2048 ||
        target.front() != '/')
    {
        return false;
    }

    for (const unsigned char character : target)
    {
        if (character <= 32 ||
            character >= 127 ||
            character == '#' ||
            character == '\\')
        {
            return false;
        }
    }

    return true;
}

std::optional<std::uint16_t> parseServiceId(
    std::string_view value
)
{
    if (value.empty() ||
        value.find_first_not_of("0123456789")
            != std::string_view::npos)
    {
        return std::nullopt;
    }

    unsigned int id = 0;

    const auto result = std::from_chars(
        value.data(),
        value.data() + value.size(),
        id
    );

    if (result.ec != std::errc{} ||
        result.ptr != value.data() + value.size() ||
        id == 0 ||
        id > std::numeric_limits<std::uint16_t>::max())
    {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(id);
}

void matchRoute(
    ApiRouteResult& result,
    ApiRoute route,
    std::string_view method,
    std::string_view allowedMethod
)
{
    result.route = route;
    result.allowedMethod = std::string(allowedMethod);

    result.status =
        method == allowedMethod
            ? ApiRouteStatus::Matched
            : ApiRouteStatus::MethodNotAllowed;
}

} // namespace

ApiRouteResult ApiRouter::resolve(
    std::string_view method,
    std::string_view target
)
{
    ApiRouteResult result;

    if (!validTarget(target))
    {
        result.status = ApiRouteStatus::InvalidTarget;

        return result;
    }

    const auto separator = target.find('?');

    const auto path = target.substr(0, separator);

    const bool hasQuery =
        separator != std::string_view::npos;

    if (hasQuery)
    {
        result.query = std::string(
            target.substr(separator + 1)
        );
    }

    if (path.find("//") != std::string_view::npos ||
        path.find('%') != std::string_view::npos)
    {
        result.status = ApiRouteStatus::InvalidTarget;

        return result;
    }

    for (const auto& entry : routes)
    {
        if (path != entry.path)
        {
            continue;
        }

        if (hasQuery)
        {
            result.status = ApiRouteStatus::InvalidTarget;

            return result;
        }

        matchRoute(
            result,
            entry.route,
            method,
            entry.method
        );

        return result;
    }

    constexpr std::string_view prefix =
        "/api/v1/services/";

    constexpr std::string_view suffix = "/epg";

    if (path.size() >= prefix.size() + suffix.size() &&
        path.substr(0, prefix.size()) == prefix &&
        path.substr(path.size() - suffix.size()) == suffix)
    {
        const auto idText = path.substr(
            prefix.size(),
            path.size() - prefix.size() - suffix.size()
        );

        result.serviceId = parseServiceId(idText);

        if (!result.serviceId)
        {
            result.status = ApiRouteStatus::InvalidTarget;

            return result;
        }

        matchRoute(
            result,
            ApiRoute::Epg,
            method,
            "GET"
        );

        return result;
    }

    return result;
}
