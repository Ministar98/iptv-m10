#include "ApiRequestHandler.hpp"

#include <chrono>
#include <exception>

ApiRequestHandler::ApiRequestHandler(
    const ServiceManager& services,
    const ApiStateCache& stateCache,
    OttMetadataManager& metadata,
    ControlCommandQueue& commands
)
    : services_(services),
      stateCache_(stateCache),
      metadata_(metadata),
      commands_(commands)
{
}

ApiResponse ApiRequestHandler::success(
    const ApiJson::Context& context,
    const ApiJson::Json& data
)
{
    ApiResponse response;

    response.statusCode = 200;
    response.body = ApiJson::success(context, data);

    return response;
}

ApiResponse ApiRequestHandler::failure(
    const ApiJson::Context& context,
    unsigned int statusCode,
    const std::string& code,
    const std::string& message,
    bool retryable
)
{
    ApiResponse response;

    response.statusCode = statusCode;

    response.body = ApiJson::failure(
        context,
        ApiJson::Error{code, message, retryable}
    );

    return response;
}

ApiResponse ApiRequestHandler::handleEpg(
    const ApiRouteResult& route,
    const ApiJson::Context& context
) const
{
    if (!route.serviceId ||
        !services_.findById(*route.serviceId))
    {
        return failure(
            context,
            404,
            "SERVICE_NOT_FOUND",
            "Requested service does not exist"
        );
    }

    std::string date;

    if (route.query.empty())
    {
        date = OttRestClient::currentUtcDate();
    }
    else
    {
        if (route.query.size() != 15 ||
            route.query.compare(0, 5, "date=") != 0)
        {
            return failure(
                context,
                400,
                "INVALID_QUERY",
                "Expected date=YYYY-MM-DD"
            );
        }

        date = route.query.substr(5);
    }

    if (!OttRestClient::isValidDate(date))
    {
        return failure(
            context,
            400,
            "INVALID_DATE",
            "Invalid EPG date"
        );
    }

    const auto result = metadata_.getEpg(
        *route.serviceId,
        date
    );

    if (!result.success())
    {
        return failure(
            context,
            503,
            "EPG_UNAVAILABLE",
            "EPG data is currently unavailable",
            true
        );
    }

    ApiJson::Json data =
    {
        {"serviceId", *route.serviceId},
        {"date", date},
        {"source",
            ottMetadataSourceToString(result.source)},
        {"stale", result.stale()},
        {"events", ApiJson::epg(result.data)}
    };

    return success(context, data);
}

ApiResponse ApiRequestHandler::handlePsi(
    const ApiStateSnapshot& snapshot,
    const ApiJson::Context& context
) const
{
    if (!snapshot.statistics)
    {
        return failure(
            context,
            503,
            "DIAGNOSTICS_UNAVAILABLE",
            "No active session statistics",
            true
        );
    }

    const auto& producer =
        snapshot.statistics->producer;

    ApiJson::Json data = {
        {"signalAvailable", producer.signalAvailable},
        {"receivedDatagrams", producer.receivedDatagrams},
        {"validRtpPackets", producer.validRtpPackets},
        {"invalidRtpPackets", producer.invalidRtpPackets},
        {"rtpDiscontinuities", producer.rtpDiscontinuities},
        {"validTsPackets", producer.validTsPackets},
        {"invalidTsPackets", producer.invalidTsPackets},
        {"transportErrors", producer.transportErrors},
        {"validPatSections", producer.validPatSections},
        {"invalidPatSections", producer.invalidPatSections},
        {"validPmtSections", producer.validPmtSections},
        {"invalidPmtSections", producer.invalidPmtSections},
        {"unexpectedPmtSections", producer.unexpectedPmtSections},
        {"detectedPrograms", producer.detectedPrograms},
        {"resolvedPmtTables", producer.resolvedPmtTables},
        {"receiveTimeouts", producer.receiveTimeouts},
        {"receiveErrors", producer.receiveErrors},
        {"programNumber", nullptr},
        {"pmtPid", nullptr},
        {"pcrPid", nullptr},
        {"streams", ApiJson::Json::array()}
    };

    if (snapshot.session.activeService &&
        producer.signalAvailable)
    {
        const auto serviceId =
            snapshot.session.activeService->serviceId;

        const auto program =
            producer.programs.find(serviceId);

        if (program != producer.programs.end())
        {
            data["programNumber"] = program->first;
            data["pmtPid"] = program->second;

            const auto pmt =
                producer.programTables.find(program->first);

            if (pmt != producer.programTables.end())
            {
                data["pcrPid"] = pmt->second.pcrPid;

                for (const auto& stream : pmt->second.streams)
                {
                    data["streams"].push_back({
                        {"pid", stream.elementaryPid},
                        {"streamType", stream.streamType},
                        {"category",
                            PmtParser::categoryName(
                                stream.category
                            )},
                        {"typeName",
                            PmtParser::streamTypeName(
                                stream.streamType
                            )}
                    });
                }
            }
        }
    }

    return success(context, data);
}

ApiResponse ApiRequestHandler::handleHealth(
    const ApiStateSnapshot& snapshot,
    const ApiJson::Context& context
) const
{
    std::int64_t snapshotAgeMs = 0;

    if (snapshot.initialized)
    {
        snapshotAgeMs =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                std::chrono::steady_clock::now() -
                snapshot.updatedAt
            ).count();
    }

    const bool ready =
        snapshot.initialized &&
        snapshot.session.state == SessionState::Running &&
        snapshot.session.playback.bufferAvailable;

    ApiJson::Json data =
    {
        {"apiAvailable", true},

        {"stateInitialized", snapshot.initialized},

        {"ready", ready},

        {"sessionState",
            sessionStateToString(
                snapshot.session.state
            )},

        {"playbackState",
            playbackStateToString(
                snapshot.session.playback.state
            )},

        {"snapshotRevision", snapshot.revision},

        {"snapshotAgeMs", snapshotAgeMs},

        {"serviceCount", services_.serviceCount()},

        {"bufferAvailable",
            snapshot.session.playback.bufferAvailable},

        {"errorMessage",
            snapshot.session.errorMessage}
    };

    return success(context, data);
}

ApiResponse ApiRequestHandler::handle(
    const ApiRequest& request
) const
{
    ApiJson::Context context;

    context.requestId = request.requestId;

    try
    {
        const auto snapshot = stateCache_.snapshot();

        context.generationId =
            snapshot.session.generationId;

        context.sessionId =
            snapshot.session.activeService
                ? snapshot.session.generationId
                : 0;

        const auto route = ApiRouter::resolve(
            request.method,
            request.target
        );

        switch (route.status)
        {
            case ApiRouteStatus::InvalidTarget:
                return failure(
                    context,
                    400,
                    "INVALID_TARGET",
                    "Invalid request target"
                );

            case ApiRouteStatus::NotFound:
                return failure(
                    context,
                    404,
                    "ROUTE_NOT_FOUND",
                    "Requested endpoint does not exist"
                );

            case ApiRouteStatus::MethodNotAllowed:
            {
                auto response = failure(
                    context,
                    405,
                    "METHOD_NOT_ALLOWED",
                    "HTTP method is not supported"
                );

                response.allowedMethod =
                    route.allowedMethod;

                return response;
            }

            case ApiRouteStatus::Matched:
                break;
        }

        if (request.method == "GET" &&
            !request.body.empty())
        {
            return failure(
                context,
                400,
                "UNEXPECTED_BODY",
                "GET request must not contain a body"
            );
        }

        switch (route.route)
        {
            case ApiRoute::Services:
                return success(
                    context,
                    ApiJson::services(
                        services_.services()
                    )
                );

            case ApiRoute::Epg:
                return handleEpg(route, context);

            case ApiRoute::Health:
                return handleHealth(snapshot, context);

            case ApiRoute::Session:
            case ApiRoute::PlaybackStatus:
            case ApiRoute::PsiDiagnostics:
            {
                if (!snapshot.initialized)
                {
                    return failure(
                        context,
                        503,
                        "STATE_UNAVAILABLE",
                        "Session state is not initialized",
                        true
                    );
                }

                if (route.route == ApiRoute::Session)
                {
                    return success(
                        context,
                        ApiJson::session(snapshot.session)
                    );
                }

                if (route.route ==
                    ApiRoute::PlaybackStatus)
                {
                    return success(
                        context,
                        ApiJson::playback(
                            snapshot.session.playback
                        )
                    );
                }

                return handlePsi(snapshot, context);
            }

            case ApiRoute::Events:
                return failure(
                    context,
                    426,
                    "UPGRADE_REQUIRED",
                    "WebSocket upgrade required"
                );

		case ApiRoute::SelectChannel:
		case ApiRoute::Pause:
		case ApiRoute::Resume:
		case ApiRoute::Seek:
		case ApiRoute::GoLive:
		case ApiRoute::AcknowledgeSeek:
		case ApiRoute::ReportPlaybackPosition:
		    return handleControl(
		        request,
		        route,
		        snapshot,
		        context
		    );

            case ApiRoute::Unknown:
                break;
        }

        return failure(
            context,
            404,
            "ROUTE_NOT_FOUND",
            "Requested endpoint does not exist"
        );
    }
    catch (const std::exception&)
    {
        return failure(
            context,
            500,
            "INTERNAL_ERROR",
            "Internal request processing error"
        );
    }
    catch (...)
    {
        return failure(
            context,
            500,
            "INTERNAL_ERROR",
            "Unknown request processing error"
        );
    }
}
ApiResponse ApiRequestHandler::handleControl(
    const ApiRequest& request,
    const ApiRouteResult& route,
    const ApiStateSnapshot& snapshot,
    ApiJson::Context context
) const
{
    using Json = ApiJson::Json;

    if (!snapshot.initialized)
    {
        return failure(
            context,
            503,
            "STATE_UNAVAILABLE",
            "Session state is not initialized",
            true
        );
    }

    Json body = request.body.empty()
        ? Json::object()
        : Json::parse(request.body, nullptr, false);

    if (body.is_discarded() || !body.is_object())
    {
        return failure(
            context,
            400,
            "INVALID_JSON",
            "Expected a JSON object"
        );
    }

    ControlCommand command;

    command.requestId = context.requestId;
    command.generationId = context.generationId;

    if (body.contains("generationId"))
    {
        const auto& generation = body.at("generationId");

        if (!generation.is_number_unsigned())
        {
            return failure(
                context,
                400,
                "INVALID_GENERATION",
                "Invalid generation ID"
            );
        }

        if (generation.get<std::uint64_t>() !=
            context.generationId)
        {
            return failure(
                context,
                409,
                "STALE_GENERATION",
                "Session generation has changed"
            );
        }
    }

    if (route.route != ApiRoute::SelectChannel &&
        snapshot.session.state != SessionState::Running)
    {
        return failure(
            context,
            409,
            "SESSION_NOT_RUNNING",
            "No running playback session"
        );
    }

    switch (route.route)
    {
        case ApiRoute::SelectChannel:
        {
            if (!body.contains("serviceId") ||
                !body.at("serviceId").is_number_unsigned())
            {
                return failure(
                    context,
                    400,
                    "INVALID_SERVICE_ID",
                    "Expected numeric serviceId"
                );
            }

            const auto serviceId =
                body.at("serviceId").get<std::uint64_t>();

            if (serviceId == 0 || serviceId > 65535)
            {
                return failure(
                    context,
                    400,
                    "INVALID_SERVICE_ID",
                    "Service ID is out of range"
                );
            }

            command.serviceId =
                static_cast<std::uint16_t>(serviceId);

            if (!services_.findById(command.serviceId))
            {
                return failure(
                    context,
                    404,
                    "SERVICE_NOT_FOUND",
                    "Requested service does not exist"
                );
            }

            command.type =
                ControlCommandType::SelectService;

            break;
        }

        case ApiRoute::Pause:
            command.type = ControlCommandType::Pause;
            break;

        case ApiRoute::Resume:
            command.type = ControlCommandType::Resume;
            break;

        case ApiRoute::Seek:
        {
            if (!body.contains("targetMs") ||
                !body.at("targetMs").is_number_integer())
            {
                return failure(
                    context,
                    400,
                    "INVALID_SEEK_POSITION",
                    "Expected integer targetMs"
                );
            }

            const auto& target = body.at("targetMs");

            if (target.is_number_unsigned() &&
                target.get<std::uint64_t>() >
                    9223372036854775807ULL)
            {
                return failure(
                    context,
                    400,
                    "INVALID_SEEK_POSITION",
                    "Seek position is out of range"
                );
            }

            command.type = ControlCommandType::Seek;

            command.seekPositionMs =
                target.get<std::int64_t>();

            break;
        }

        case ApiRoute::GoLive:
            command.type = ControlCommandType::GoLive;
            break;

	case ApiRoute::AcknowledgeSeek:
	case ApiRoute::ReportPlaybackPosition:
	{
	    if (!body.contains("generationId") ||
	        !body.contains("seekRevision") ||
	        !body.contains("positionMs") ||
	        !body.at("seekRevision").is_number_unsigned() ||
	        !body.at("positionMs").is_number_unsigned())
	    {
	        return failure(
	            context,
	            400,
	            "INVALID_PLAYBACK_FEEDBACK",
	            "Expected generationId, seekRevision and positionMs"
	        );
	    }

	    command.type =
	        route.route == ApiRoute::AcknowledgeSeek
	            ? ControlCommandType::AcknowledgeSeek
	            : ControlCommandType::ReportPlaybackPosition;

	    command.seekRevision =
	        body.at("seekRevision").get<std::uint64_t>();

	    command.playbackPositionMs =
	        body.at("positionMs").get<std::uint64_t>();

	    break;
	}

        default:
            return failure(
                context,
                404,
                "ROUTE_NOT_FOUND",
                "Unknown control endpoint"
            );
    }

    const auto submitted = commands_.submit(command);

    if (!submitted.accepted())
    {
        if (submitted.status == CommandSubmitStatus::Full)
        {
            return failure(
                context,
                503,
                "QUEUE_FULL",
                "Control command queue is full",
                true
            );
        }

        return failure(
            context,
            503,
            "CONTROL_UNAVAILABLE",
            "Control pipeline is unavailable",
            true
        );
    }

    context.operationId = submitted.operationId;

    ApiResponse response;

    response.statusCode = 202;
    response.body = ApiJson::accepted(context);

    return response;
}
