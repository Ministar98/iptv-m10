#include "ApiRequestHandler.hpp"

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

ApiRequest makeRequest(
    const std::string& method,
    const std::string& target
)
{
    ApiRequest request;

    request.method = method;
    request.target = target;
    request.requestId = "test-request-1";

    return request;
}

void requireError(
    const ApiResponse& response,
    unsigned int statusCode,
    const std::string& code
)
{
    require(
        response.statusCode == statusCode,
        "Unexpected HTTP status"
    );

    require(
        response.body.at("status") == "ERROR",
        "Expected error response"
    );

    require(
        response.body.at("error").at("code") == code,
        "Unexpected error code"
    );
}

void testHandler()
{
    const ServiceManager services;

    ApiStateCache cache;

    OttMetadataManager metadata;

    ControlCommandQueue commands(128);

    ApiRequestHandler handler(
        services,
        cache,
        metadata,
	commands
    );

    auto response = handler.handle(
        makeRequest("GET", "/api/v1/services")
    );

    require(response.statusCode == 200,
            "Services HTTP status");

    require(response.body.at("status") == "OK",
            "Services response status");

    require(response.body.at("data").size() == 3,
            "Service count");

    pass("Service catalog response");

    require(
        response.body.at("requestId") ==
            "test-request-1",
        "Request ID correlation"
    );

    pass("Request ID preserved");

    response = handler.handle(
        makeRequest("GET", "/api/v1/health")
    );

    require(response.statusCode == 200,
            "Initial health status");

    require(
        response.body.at("data").at("ready") == false,
        "Initial readiness"
    );

    pass("Health before initialization");

    response = handler.handle(
        makeRequest("GET", "/api/v1/session")
    );

    requireError(
        response,
        503,
        "STATE_UNAVAILABLE"
    );

    pass("Uninitialized state rejected");

    SessionSnapshot session;

    session.state = SessionState::Running;
    session.generationId = 1;
    session.activeService = services.firstService();

    session.playback.generationId = 1;
    session.playback.state = PlaybackState::Live;
    session.playback.bufferAvailable = true;
    session.playback.livePositionMs = 10000;
    session.playback.playbackPositionMs = 10000;
    session.playback.bufferDurationMs = 10000;
    session.playback.atLiveEdge = true;

    ActiveSessionStatistics statistics;

    statistics.producer.signalAvailable = true;
    statistics.producer.validPatSections = 20;
    statistics.producer.validPmtSections = 20;
    statistics.producer.detectedPrograms = 1;
    statistics.producer.resolvedPmtTables = 1;

    require(
        cache.publish(session, statistics),
        "State publication"
    );

    response = handler.handle(
        makeRequest("GET", "/api/v1/session")
    );

    require(response.statusCode == 200,
            "Session HTTP status");

    require(
        response.body.at("data").at("serviceId") == 1,
        "Active service"
    );

    require(
        response.body.at("generationId") == 1,
        "Session generation"
    );

    pass("Active session response");

    response = handler.handle(
        makeRequest("GET", "/api/v1/playback/status")
    );

    require(response.statusCode == 200,
            "Playback HTTP status");

    require(
        response.body.at("data").at("state") == "LIVE",
        "Playback state"
    );

    require(
        response.body.at("data").at("positionMs") ==
            10000,
        "Playback position"
    );

    pass("Playback status response");

    response = handler.handle(
        makeRequest("GET", "/api/v1/diagnostics/psi")
    );

    require(response.statusCode == 200,
            "Diagnostics HTTP status");

    require(
        response.body.at("data").at("validPatSections")
            == 20,
        "PAT statistics"
    );

    require(
        response.body.at("data").at("validPmtSections")
            == 20,
        "PMT statistics"
    );

    pass("PSI diagnostics response");

    response = handler.handle(
        makeRequest("GET", "/api/v1/health")
    );

    require(
        response.body.at("data").at("ready") == true,
        "Session readiness"
    );

    pass("Ready session health");

    response = handler.handle(
        makeRequest(
            "GET",
            "/api/v1/services/999/epg"
        )
    );

    requireError(
        response,
        404,
        "SERVICE_NOT_FOUND"
    );

    pass("Unknown EPG service rejected");

    response = handler.handle(
        makeRequest(
            "GET",
            "/api/v1/services/1/epg?date=2026-02-30"
        )
    );

    requireError(
        response,
        400,
        "INVALID_DATE"
    );

    pass("Invalid EPG date rejected");

    response = handler.handle(
        makeRequest(
            "GET",
            "/api/v1/services/1/epg?day=today"
        )
    );

    requireError(
        response,
        400,
        "INVALID_QUERY"
    );

    pass("Invalid EPG query rejected");

    response = handler.handle(
        makeRequest("GET", "/api/v1/unknown")
    );

    requireError(
        response,
        404,
        "ROUTE_NOT_FOUND"
    );

    pass("Unknown endpoint rejected");

    response = handler.handle(
        makeRequest("POST", "/api/v1/services")
    );

    requireError(
        response,
        405,
        "METHOD_NOT_ALLOWED"
    );

    require(response.allowedMethod == "GET",
            "Allowed method");

    pass("HTTP method validation");

    response = handler.handle(
        makeRequest("GET", "/api/v1//services")
    );

    requireError(
        response,
        400,
        "INVALID_TARGET"
    );

    pass("Invalid target rejected");

    auto request = makeRequest(
        "GET",
        "/api/v1/services"
    );

    request.body = "{}";

    response = handler.handle(request);

    requireError(
        response,
        400,
        "UNEXPECTED_BODY"
    );

    pass("Unexpected GET body rejected");

	response = handler.handle(
	    makeRequest("POST", "/api/v1/playback/pause")
	);

	require(
	    response.statusCode == 202,
	    "Pause command accepted"
	);

	require(
	    response.body.at("status") == "ACCEPTED",
	    "Accepted response status"
	);

	const auto pauseCommand = commands.tryPop();

	require(
	    pauseCommand.has_value(),
	    "Pause command queued"
	);

	require(
	    pauseCommand->type == ControlCommandType::Pause,
	    "Pause command type"
	);

	require(
	    pauseCommand->generationId == 1,
	    "Pause command generation"
	);

	pass("Pause command accepted and queued");

	request = makeRequest(
	    "POST",
	    "/api/v1/session/channel"
	);

	request.body = R"({"serviceId":2})";

	response = handler.handle(request);

	require(
	    response.statusCode == 202,
	    "Channel command accepted"
	);

	const auto channelCommand = commands.tryPop();

	require(
	    channelCommand.has_value() &&
	    channelCommand->serviceId == 2 &&
	    channelCommand->type ==
	        ControlCommandType::SelectService,
	    "Channel command queued"
	);

	pass("Channel selection accepted");

	request.body = R"({"serviceId":999})";

	requireError(
	    handler.handle(request),
	    404,
	    "SERVICE_NOT_FOUND"
	);

	pass("Unknown service rejected");

	request = makeRequest(
	    "POST",
	    "/api/v1/playback/seek"
	);

	request.body = R"({"targetMs":2000})";

	response = handler.handle(request);

	require(
	    response.statusCode == 202,
	    "Seek command accepted"
	);

	const auto seekCommand = commands.tryPop();

	require(
	    seekCommand.has_value() &&
	    seekCommand->seekPositionMs == 2000,
	    "Seek position preserved"
	);

	pass("Seek command accepted");

	request.body = R"({"targetMs":"invalid"})";

	requireError(
	    handler.handle(request),
	    400,
	    "INVALID_SEEK_POSITION"
	);

	pass("Invalid seek position rejected");

	request.body = R"({"targetMs":2000,"generationId":0})";

	requireError(
	    handler.handle(request),
	    409,
	    "STALE_GENERATION"
	);

	pass("Stale generation rejected");

	request.body = "{invalid";

	requireError(
	    handler.handle(request),
	    400,
	    "INVALID_JSON"
	);

	pass("Invalid JSON rejected");

    pass("Pending control endpoint response");

    response = handler.handle(
        makeRequest("GET", "/api/v1/events")
    );

    requireError(
        response,
        426,
        "UPGRADE_REQUIRED"
    );

    pass("WebSocket upgrade response");

    require(
        cache.publish(session, std::nullopt),
        "Statistics removal"
    );

    response = handler.handle(
        makeRequest("GET", "/api/v1/diagnostics/psi")
    );

    requireError(
        response,
        503,
        "DIAGNOSTICS_UNAVAILABLE"
    );

    pass("Unavailable diagnostics response");
}

} // namespace

int main()
{
    testHandler();

    std::cout
        << "\nAll API Request Handler tests passed.\n";

    return EXIT_SUCCESS;
}
