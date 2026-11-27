#include "ApiRouter.hpp"

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << '\n';

        std::exit(EXIT_FAILURE);
    }
}

void pass(const char* message)
{
    std::cout << "[PASS] " << message << '\n';
}

void checkRoute(
    const char* method,
    const char* path,
    ApiRoute expected
)
{
    const auto result = ApiRouter::resolve(method, path);

    require(result.matched(), path);

    require(result.route == expected, path);
}

void testGetRoutes()
{
    checkRoute(
        "GET", "/api/v1/services",
        ApiRoute::Services
    );

    checkRoute(
        "GET", "/api/v1/session",
        ApiRoute::Session
    );

    checkRoute(
        "GET", "/api/v1/playback/status",
        ApiRoute::PlaybackStatus
    );

    checkRoute(
        "GET", "/api/v1/diagnostics/psi",
        ApiRoute::PsiDiagnostics
    );

    checkRoute(
        "GET", "/api/v1/health",
        ApiRoute::Health
    );

    pass("All static GET routes");
}

void testPostRoutes()
{
    checkRoute(
        "POST", "/api/v1/session/channel",
        ApiRoute::SelectChannel
    );

    checkRoute(
        "POST", "/api/v1/playback/pause",
        ApiRoute::Pause
    );

    checkRoute(
        "POST", "/api/v1/playback/resume",
        ApiRoute::Resume
    );

    checkRoute(
        "POST", "/api/v1/playback/seek",
        ApiRoute::Seek
    );

    checkRoute(
        "POST", "/api/v1/playback/live",
        ApiRoute::GoLive
    );

    pass("All POST routes");
}

void testEpgRoute()
{
    const auto result = ApiRouter::resolve(
        "GET",
        "/api/v1/services/2/epg?date=2026-11-26"
    );

    require(result.matched(), "EPG route");

    require(result.route == ApiRoute::Epg,
            "EPG route type");

    require(result.serviceId == 2,
            "EPG service ID");

    require(result.query == "date=2026-11-26",
            "EPG query");

    pass("EPG route with service ID and query");

    checkRoute(
        "GET",
        "/api/v1/services/1/epg",
        ApiRoute::Epg
    );

    pass("EPG route without query");

    const auto maximum = ApiRouter::resolve(
        "GET",
        "/api/v1/services/65535/epg"
    );

    require(maximum.matched(), "Maximum service ID");

    require(maximum.serviceId == 65535,
            "Maximum service ID value");

    pass("Maximum valid service ID");
}

void testInvalidServiceIds()
{
    const char* paths[] =
    {
        "/api/v1/services/0/epg",
        "/api/v1/services/-1/epg",
        "/api/v1/services/abc/epg",
        "/api/v1/services/65536/epg",
        "/api/v1/services/999999999999999999/epg",
        "/api/v1/services/1/2/epg",
        "/api/v1/services//epg"
    };

    for (const auto* path : paths)
    {
        const auto result = ApiRouter::resolve(
            "GET", path
        );

        require(
            result.status == ApiRouteStatus::InvalidTarget,
            path
        );
    }

    pass("Invalid service IDs rejected");
}

void testMethodValidation()
{
    auto result = ApiRouter::resolve(
        "POST",
        "/api/v1/services"
    );

    require(
        result.status == ApiRouteStatus::MethodNotAllowed,
        "Invalid GET method"
    );

    require(result.allowedMethod == "GET",
            "Allowed GET method");

    result = ApiRouter::resolve(
        "GET",
        "/api/v1/playback/pause"
    );

    require(
        result.status == ApiRouteStatus::MethodNotAllowed,
        "Invalid POST method"
    );

    require(result.allowedMethod == "POST",
            "Allowed POST method");

    pass("HTTP method validation");

    result = ApiRouter::resolve(
        "DELETE",
        "/api/v1/services/1/epg"
    );

    require(
        result.status == ApiRouteStatus::MethodNotAllowed,
        "Invalid EPG method"
    );

    pass("EPG method validation");
}

void testInvalidTargets()
{
    const char* targets[] =
    {
        "",
        "api/v1/services",
        "http://localhost/api/v1/services",
        "/api/v1/services#fragment",
        "/api/v1/ser vices",
        "/api/v1//services",
        "/api/v1/%73ervices",
        "/api/v1/services\\",
        "/api/v1/health?unexpected=1"
    };

    for (const auto* target : targets)
    {
        const auto result = ApiRouter::resolve(
            "GET", target
        );

        require(
            result.status == ApiRouteStatus::InvalidTarget,
            target
        );
    }

    pass("Invalid request targets rejected");

    const std::string oversized =
        "/" + std::string(2048, 'a');

    require(
        ApiRouter::resolve("GET", oversized).status ==
            ApiRouteStatus::InvalidTarget,
        "Oversized target"
    );

    pass("Oversized target rejected");
}

void testUnknownRoutes()
{
    const auto result = ApiRouter::resolve(
        "GET",
        "/api/v1/unknown"
    );

    require(
        result.status == ApiRouteStatus::NotFound,
        "Unknown route"
    );

    require(result.route == ApiRoute::Unknown,
            "Unknown route type");

    pass("Unknown route rejected");
}

void testWebSocketRoute()
{
    checkRoute(
        "GET",
        "/api/v1/events",
        ApiRoute::Events
    );

    pass("WebSocket upgrade route");
}

} // namespace

int main()
{
    testGetRoutes();
    testPostRoutes();
    testEpgRoute();
    testInvalidServiceIds();
    testMethodValidation();
    testInvalidTargets();
    testUnknownRoutes();
    testWebSocketRoute();

    std::cout << "\nAll API Router tests passed.\n";

    return EXIT_SUCCESS;
}
