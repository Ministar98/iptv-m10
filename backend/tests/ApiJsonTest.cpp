#include "ApiJson.hpp"

#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>

namespace
{

using ApiJson::Json;

void require(
    bool condition,
    const std::string& message
)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void pass(const std::string& message)
{
    std::cout << "[PASS] " << message << '\n';
}

void testServices()
{
    TvService service;

    service.serviceId = 2;
    service.channelNumber = 2;
    service.name = "Channel 2";
    service.description = "Test channel";

    const auto result =
        ApiJson::services({service});

    require(
        result.is_array() &&
        result.size() == 1 &&
        result[0]["serviceId"] == 2 &&
        result[0]["channelNumber"] == 2 &&
        result[0]["name"] == "Channel 2" &&
        result[0]["streamProfile"]["protocol"] == "RTP",
        "Invalid service serialization"
    );

    pass("Service catalog serialization");

    const auto empty = ApiJson::services({});

    require(
        empty.is_array() && empty.empty(),
        "Invalid empty service catalog"
    );

    pass("Empty service catalog");
}

void testEpg()
{
    EpgEvent event;

    event.eventId = "event-1";
    event.serviceId = 1;
    event.title = "Morning News";
    event.description = "Daily news";
    event.startTime = "2026-11-26T08:00:00.000Z";
    event.endTime = "2026-11-26T09:00:00.000Z";
    event.startTimeMs = 1795680000000LL;
    event.endTimeMs = 1795683600000LL;

    const auto result = ApiJson::epg({event});

    require(
        result.is_array() &&
        result.size() == 1 &&
        result[0]["eventId"] == event.eventId &&
        result[0]["serviceId"] == event.serviceId &&
        result[0]["title"] == event.title &&
        result[0]["startTime"] == event.startTime &&
        result[0]["endTime"] == event.endTime &&
        result[0]["startTimeMs"] == event.startTimeMs &&
        result[0]["endTimeMs"] == event.endTimeMs,
        "Invalid EPG serialization"
    );

    pass("EPG serialization");

    const auto empty = ApiJson::epg({});

    require(
        empty.is_array() && empty.empty(),
        "Invalid empty EPG"
    );

    pass("Empty EPG");
}

void testPlayback()
{
    PlaybackSnapshot snapshot;

    snapshot.state = PlaybackState::Timeshift;
    snapshot.generationId = 5;
    snapshot.bufferAvailable = true;

    snapshot.oldestPositionMs = 10000;
    snapshot.livePositionMs = 50000;
    snapshot.playbackPositionMs = 30000;
    snapshot.delayFromLiveMs = 20000;
    snapshot.bufferDurationMs = 40000;
    snapshot.positionInBufferMs = 20000;

    snapshot.seekRequired = true;
    snapshot.seekRevision = 3;

    const auto result = ApiJson::playback(snapshot);

    require(
        result["state"] == "TIMESHIFT" &&
        result["generationId"] == 5 &&
        result["bufferAvailable"] == true &&
        result["earliestMs"] == 10000 &&
        result["latestMs"] == 50000 &&
        result["positionMs"] == 30000 &&
        result["liveDelayMs"] == 20000 &&
        result["bufferDurationMs"] == 40000 &&
        result["positionInBufferMs"] == 20000 &&
        result["seekRequired"] == true &&
        result["seekRevision"] == 3,
        "Invalid playback serialization"
    );

    pass("Playback status serialization");
}

void testSession()
{
    SessionSnapshot snapshot;

    auto result = ApiJson::session(snapshot);

    require(
        result["state"] == "IDLE" &&
        result["sessionId"] == 0 &&
        result["serviceId"].is_null() &&
        result["activeService"].is_null(),
        "Invalid idle session"
    );

    pass("Idle session serialization");

    snapshot.state = SessionState::Running;
    snapshot.generationId = 7;

    TvService service;

    service.serviceId = 3;
    service.channelNumber = 3;
    service.name = "Channel 3";

    snapshot.activeService = service;
    snapshot.playback.generationId = 7;
    snapshot.playback.state = PlaybackState::Live;

    result = ApiJson::session(snapshot);

    require(
        result["state"] == "RUNNING" &&
        result["sessionId"] == 7 &&
        result["generationId"] == 7 &&
        result["serviceId"] == 3 &&
        result["activeService"]["name"] == "Channel 3" &&
        result["playback"]["state"] == "LIVE",
        "Invalid active session"
    );

    pass("Active session serialization");
}

void testResponses()
{
    ApiJson::Context context;

    context.requestId = "request-1";
    context.sessionId = 5;
    context.generationId = 5;

    auto result = ApiJson::success(
        context,
        {{"value", 42}}
    );

    require(
        result["requestId"] == "request-1" &&
        result["sessionId"] == 5 &&
        result["generationId"] == 5 &&
        result["status"] == "OK" &&
        result["data"]["value"] == 42 &&
        !result.contains("operationId") &&
        !result.contains("error"),
        "Invalid success envelope"
    );

    pass("Success response envelope");

    bool rejected = false;

    try
    {
        ApiJson::accepted(context);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    require(
        rejected,
        "Accepted response without operationId"
    );

    pass("Missing operationId rejected");

    context.operationId = 12;

    result = ApiJson::accepted(context);

    require(
        result["status"] == "ACCEPTED" &&
        result["operationId"] == 12,
        "Invalid accepted response"
    );

    pass("Accepted operation envelope");

    result = ApiJson::failure(
        context,
        {
            "OTT_UNAVAILABLE",
            "OTT server unavailable",
            true
        }
    );

    require(
        result["status"] == "ERROR" &&
        result["operationId"] == 12 &&
        result["error"]["code"] == "OTT_UNAVAILABLE" &&
        result["error"]["message"] ==
            "OTT server unavailable" &&
        result["error"]["retryable"] == true,
        "Invalid error response"
    );

    pass("Error response envelope");
}

void testEvents()
{
    ApiJson::Context context;

    context.sessionId = 8;
    context.generationId = 8;
    context.operationId = 15;

    const auto result = ApiJson::event(
        "CHANNEL_READY",
        context,
        {{"serviceId", 2}}
    );

    require(
        result["type"] == "CHANNEL_READY" &&
        result["sessionId"] == 8 &&
        result["generationId"] == 8 &&
        result["operationId"] == 15 &&
        result["payload"]["serviceId"] == 2,
        "Invalid WebSocket event"
    );

    pass("WebSocket event envelope");

    const std::regex timestampPattern(
        R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z$)"
    );

    require(
        std::regex_match(
            result["timestamp"].get<std::string>(),
            timestampPattern
        ),
        "Invalid event timestamp"
    );

    pass("ISO 8601 UTC timestamp");

    context.operationId.reset();

    const auto periodic = ApiJson::event(
        "PLAYBACK_POSITION",
        context,
        {{"positionMs", 12000}}
    );

    require(
        !periodic.contains("operationId") &&
        periodic["payload"]["positionMs"] == 12000,
        "Invalid periodic event"
    );

    pass("Event without operationId");

    const auto decoded =
        Json::parse(result.dump());

    require(
        decoded == result,
        "JSON round-trip failed"
    );

    pass("JSON serialization round-trip");
}

} // namespace

int main()
{
    try
    {
        testServices();
        testEpg();
        testPlayback();
        testSession();
        testResponses();
        testEvents();

        std::cout
            << "\nAll API JSON tests passed.\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "[FAIL] "
            << error.what()
            << '\n';

        return 1;
    }
}
