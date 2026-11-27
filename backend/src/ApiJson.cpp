#include "ApiJson.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ApiJson
{

namespace
{

Json envelope(
    const Context& context,
    const std::string& status
)
{
    Json result = {
        {"requestId", context.requestId},
        {"sessionId", context.sessionId},
        {"generationId", context.generationId},
        {"status", status}
    };

    if (context.operationId)
    {
        result["operationId"] = *context.operationId;
    }

    return result;
}

} // namespace

Json service(const TvService& value)
{
    return {
        {"serviceId", value.serviceId},
        {"channelNumber", value.channelNumber},
        {"name", value.name},
        {"description", value.description},
        {
            "streamProfile",
            {
                {"protocol", "RTP"},
                {"container", "MPEG-TS"},
                {"videoCodec", "H.264"},
                {"audioCodec", "AAC"},
                {"rtpAddress", value.rtpAddress},
                {"rtpPort", value.rtpPort}
            }
        }
    };
}

Json services(const std::vector<TvService>& values)
{
    Json result = Json::array();

    for (const auto& value : values)
    {
        result.push_back(service(value));
    }

    return result;
}

Json epgEvent(const EpgEvent& value)
{
    return {
        {"eventId", value.eventId},
        {"serviceId", value.serviceId},
        {"title", value.title},
        {"description", value.description},
        {"startTime", value.startTime},
        {"endTime", value.endTime},
        {"startTimeMs", value.startTimeMs},
        {"endTimeMs", value.endTimeMs}
    };
}

Json epg(const std::vector<EpgEvent>& values)
{
    Json result = Json::array();

    for (const auto& value : values)
    {
        result.push_back(epgEvent(value));
    }

    return result;
}

Json playback(const PlaybackSnapshot& snapshot)
{
    return {
        {
            "state",
            playbackStateToString(snapshot.state)
        },
        {"generationId", snapshot.generationId},
        {"bufferAvailable", snapshot.bufferAvailable},
        {"earliestMs", snapshot.oldestPositionMs},
        {"latestMs", snapshot.livePositionMs},
        {"positionMs", snapshot.playbackPositionMs},
        {"liveDelayMs", snapshot.delayFromLiveMs},
        {"bufferDurationMs", snapshot.bufferDurationMs},
        {"positionInBufferMs", snapshot.positionInBufferMs},
        {"atLiveEdge", snapshot.atLiveEdge},
        {"positionExpired", snapshot.positionExpired},
        {"seekRequired", snapshot.seekRequired},
        {"seekRevision", snapshot.seekRevision},
        {"errorMessage", snapshot.errorMessage}
    };
}

Json session(const SessionSnapshot& snapshot)
{
    const bool active =
        snapshot.activeService.has_value();

    Json result = {
        {"state", sessionStateToString(snapshot.state)},
        {"sessionId", active ? snapshot.generationId : 0},
        {"generationId", snapshot.generationId},
        {"serviceId", nullptr},
        {"activeService", nullptr},
        {"hlsUrl", nullptr},
        {"playback", playback(snapshot.playback)},
        {"errorMessage", snapshot.errorMessage}
    };

    if (active)
    {
        result["serviceId"] =
            snapshot.activeService->serviceId;

        result["activeService"] =
            service(*snapshot.activeService);

        result["hlsUrl"] =
            "/hls/" +
            std::to_string(snapshot.generationId) +
            "/index.m3u8";
    }

    return result;
}

Json success(
    const Context& context,
    const Json& data
)
{
    Json result = envelope(context, "OK");

    result["data"] = data;

    return result;
}

Json accepted(const Context& context)
{
    if (!context.operationId)
    {
        throw std::invalid_argument(
            "Accepted response requires operationId"
        );
    }

    return envelope(context, "ACCEPTED");
}

Json failure(
    const Context& context,
    const Error& error
)
{
    Json result = envelope(context, "ERROR");

    result["error"] = {
        {"code", error.code},
        {"message", error.message},
        {"retryable", error.retryable}
    };

    return result;
}

std::string utcTimestamp()
{
    using namespace std::chrono;

    const auto now = system_clock::now();

    const auto seconds =
        floor<std::chrono::seconds>(now);

    const auto milliseconds =
        duration_cast<std::chrono::milliseconds>(
            now - seconds
        ).count();

    const std::time_t time =
        system_clock::to_time_t(seconds);

    std::tm utc{};

    if (::gmtime_r(&time, &utc) == nullptr)
    {
        throw std::runtime_error(
            "UTC timestamp conversion failed"
        );
    }

    std::ostringstream output;

    output
        << std::put_time(
            &utc,
            "%Y-%m-%dT%H:%M:%S"
        )
        << '.'
        << std::setfill('0')
        << std::setw(3)
        << milliseconds
        << 'Z';

    return output.str();
}

Json event(
    const std::string& type,
    const Context& context,
    const Json& payload
)
{
    Json result = {
        {"type", type},
        {"timestamp", utcTimestamp()},
        {"sessionId", context.sessionId},
        {"generationId", context.generationId},
        {"payload", payload}
    };

    if (context.operationId)
    {
        result["operationId"] = *context.operationId;
    }

    return result;
}

} // namespace ApiJson
