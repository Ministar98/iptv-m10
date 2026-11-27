#include "ApiOperationJson.hpp"

namespace ApiJson
{

Json operationResult(
    const ControlOperationResult& result
)
{
    Context context;

    context.requestId = result.command.requestId;
    context.operationId = result.command.operationId;
    context.generationId = result.session.generationId;

    context.sessionId =
        result.session.activeService
            ? result.session.generationId
            : 0;

    Json payload = {
        {
            "command",
            controlCommandTypeToString(
                result.command.type
            )
        },
        {
            "status",
            controlOperationStatusToString(
                result.status
            )
        },
        {
            "requestedGenerationId",
            result.command.generationId
        },
        {"session", session(result.session)}
    };

    if (!result.success())
    {
        payload["error"] = {
            {"code", result.errorCode},
            {"message", result.errorMessage}
        };
    }

    auto response = event(
        "OPERATION_RESULT",
        context,
        payload
    );

    response["requestId"] = context.requestId;

    return response;
}

}
