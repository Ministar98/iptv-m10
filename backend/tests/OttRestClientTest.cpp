#include "OttRestClient.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

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

} // namespace

int main()
{
    try
    {
        const OttRestClient client;

        const auto services = client.fetchServices();

        require(
            services.success(),
            "Service request failed: " +
                services.errorMessage
        );

        require(
            services.httpStatus == 200,
            "Unexpected HTTP status"
        );

        require(
            services.data.size() == 3,
            "Expected three services"
        );

        pass("Fetch service catalog");

        for (std::size_t i = 0;
             i < services.data.size();
             ++i)
        {
            const auto& service = services.data[i];

            require(
                service.serviceId == i + 1,
                "Unexpected service ID"
            );

            require(
                service.channelNumber == i + 1,
                "Unexpected channel number"
            );

            require(
                service.rtpPort == 5004,
                "Unexpected RTP port"
            );

            std::cout
                << "  Service: "
                << service.serviceId
                << " - "
                << service.name
                << '\n';
        }

        pass("Validate service catalog");

        const std::string date =
            OttRestClient::currentUtcDate();

        require(
            OttRestClient::isValidDate(date),
            "Invalid current UTC date"
        );

        pass("Current UTC date");

        for (const auto& service : services.data)
        {
            const auto epg = client.fetchEpg(
                service.serviceId,
                date
            );

            require(
                epg.success(),
                "EPG request failed: " +
                    epg.errorMessage
            );

            require(
                epg.data.size() == 36,
                "Expected 36 EPG events"
            );

            for (const auto& event : epg.data)
            {
                require(
                    event.serviceId == service.serviceId,
                    "Unexpected EPG service ID"
                );

                require(
                    event.endTimeMs > event.startTimeMs,
                    "Invalid EPG event duration"
                );
            }

            for (std::size_t i = 1;
                 i < epg.data.size();
                 ++i)
            {
                require(
                    epg.data[i - 1].endTimeMs ==
                    epg.data[i].startTimeMs,
                    "EPG gap or overlap"
                );
            }

            const auto duration =
                epg.data.back().endTimeMs -
                epg.data.front().startTimeMs;

            require(
                duration == 86400000,
                "EPG must cover 24 hours"
            );

            pass(
                "Fetch and validate EPG for " +
                service.name
            );
        }

        const auto unknown = client.fetchEpg(99, date);

        require(
            unknown.status == OttRestStatus::HttpError &&
            unknown.httpStatus == 404,
            "Expected HTTP 404"
        );

        pass("Unknown service handling");

        const auto invalidDate =
            client.fetchEpg(1, "2026-02-30");

        require(
            invalidDate.status ==
                OttRestStatus::InvalidRequest,
            "Invalid date was not rejected"
        );

        pass("Invalid date handling");

        const auto invalidService =
            client.fetchEpg(0, date);

        require(
            invalidService.status ==
                OttRestStatus::InvalidRequest,
            "Invalid service ID was not rejected"
        );

        pass("Invalid service ID handling");

        require(
            OttRestClient::isValidDate("2028-02-29") &&
            !OttRestClient::isValidDate("2026-02-29") &&
            !OttRestClient::isValidDate("2026-13-01") &&
            !OttRestClient::isValidDate("2026-00-01") &&
            !OttRestClient::isValidDate("2026-01-00") &&
            !OttRestClient::isValidDate("invalid"),
            "Calendar validation failed"
        );

        pass("Calendar date validation");

        OttRestConfig limitedConfig;

        limitedConfig.maxResponseBytes = 64;

        const OttRestClient limitedClient(
            limitedConfig
        );

        const auto limited =
            limitedClient.fetchServices();

        require(
            limited.status ==
                OttRestStatus::InvalidResponse,
            "Response size limit was not enforced"
        );

        pass("Response size limit");

        std::cout
            << "\nAll OTT REST Client tests passed.\n";

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
