#pragma once

#include "ApiRouter.hpp"
#include "ApiJson.hpp"
#include "ApiStateCache.hpp"
#include "OttMetadataManager.hpp"
#include "ServiceManager.hpp"
#include "ControlCommandQueue.hpp"

#include <string>

struct ApiRequest
{
    std::string method;
    std::string target;
    std::string requestId;
    std::string body;
};

struct ApiResponse
{
    unsigned int statusCode = 200;

    std::string contentType = "application/json";
    std::string allowedMethod;

    ApiJson::Json body;
};

class ApiRequestHandler
{
public:
    ApiRequestHandler(
        const ServiceManager& services,
        const ApiStateCache& stateCache,
        OttMetadataManager& metadata,
        ControlCommandQueue& commands
    );

    ApiResponse handle(const ApiRequest& request) const;

private:
    const ServiceManager& services_;
    const ApiStateCache& stateCache_;
    OttMetadataManager& metadata_;
    ControlCommandQueue& commands_;

    static ApiResponse success(
        const ApiJson::Context& context,
        const ApiJson::Json& data
    );

    static ApiResponse failure(
        const ApiJson::Context& context,
        unsigned int statusCode,
        const std::string& code,
        const std::string& message,
        bool retryable = false
    );

    ApiResponse handleEpg(
        const ApiRouteResult& route,
        const ApiJson::Context& context
    ) const;

    ApiResponse handlePsi(
        const ApiStateSnapshot& snapshot,
        const ApiJson::Context& context
    ) const;

    ApiResponse handleHealth(
        const ApiStateSnapshot& snapshot,
        const ApiJson::Context& context
    ) const;

    ApiResponse handleControl(
        const ApiRequest& request,
        const ApiRouteResult& route,
        const ApiStateSnapshot& snapshot,
        ApiJson::Context context
    ) const;
};
