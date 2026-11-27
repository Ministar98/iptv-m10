#pragma once

#include "SessionSnapshot.hpp"
#include "EpgEvent.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ApiJson
{

using Json = nlohmann::json;

struct Context
{
    std::string requestId;

    std::uint64_t sessionId = 0;
    std::uint64_t generationId = 0;

    std::optional<std::uint64_t> operationId;
};

struct Error
{
    std::string code;
    std::string message;

    bool retryable = false;
};

Json service(const TvService& value);

Json services(const std::vector<TvService>& values);

Json epgEvent(const EpgEvent& value);

Json epg(const std::vector<EpgEvent>& values);

Json playback(const PlaybackSnapshot& snapshot);

Json session(const SessionSnapshot& snapshot);

Json success(
    const Context& context,
    const Json& data
);

Json accepted(const Context& context);

Json failure(
    const Context& context,
    const Error& error
);

Json event(
    const std::string& type,
    const Context& context,
    const Json& payload
);

std::string utcTimestamp();

} // namespace ApiJson
