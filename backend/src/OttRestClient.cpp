#include "OttRestClient.hpp"
#include "ServiceManager.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{

using Json = nlohmann::json;

class CurlRuntime
{
public:
    CurlRuntime()
        : result_(curl_global_init(CURL_GLOBAL_DEFAULT))
    {
    }

    ~CurlRuntime()
    {
        if (result_ == CURLE_OK)
        {
            curl_global_cleanup();
        }
    }

    bool available() const noexcept
    {
        return result_ == CURLE_OK;
    }

private:
    CURLcode result_;
};

CurlRuntime& curlRuntime()
{
    static CurlRuntime runtime;

    return runtime;
}

struct ResponseBuffer
{
    std::string data;

    std::size_t limit = 0;

    bool overflow = false;

    bool writeFailed = false;
};

std::size_t writeResponse(
    char* data,
    std::size_t size,
    std::size_t count,
    void* userData
) noexcept
{
    auto& response =
        *static_cast<ResponseBuffer*>(userData);

    if (
        size != 0 &&
        count > std::numeric_limits<std::size_t>::max() / size
    )
    {
        response.overflow = true;

        return 0;
    }

    const std::size_t bytes = size * count;

    if (bytes > response.limit - response.data.size())
    {
        response.overflow = true;

        return 0;
    }

    try
    {
        response.data.append(data, bytes);
    }
    catch (...)
    {
        response.writeFailed = true;

        return 0;
    }

    return bytes;
}

template<typename T>
OttRestResult<T> makeError(
    OttRestStatus status,
    const std::string& message,
    long httpStatus = 0
)
{
    OttRestResult<T> result;

    result.status = status;
    result.httpStatus = httpStatus;
    result.errorMessage = message;

    return result;
}

template<typename T>
OttRestResult<T> forwardError(
    const OttRestResult<std::string>& source
)
{
    return makeError<T>(
        source.status,
        source.errorMessage,
        source.httpStatus
    );
}

bool isDigits(
    const std::string& value,
    std::size_t offset,
    std::size_t length
)
{
    if (offset > value.size() ||
        length > value.size() - offset)
    {
        return false;
    }

    for (std::size_t i = offset;
         i < offset + length;
         ++i)
    {
        if (value[i] < '0' || value[i] > '9')
        {
            return false;
        }
    }

    return true;
}

bool isLeapYear(int year)
{
    return year % 4 == 0 &&
        (year % 100 != 0 || year % 400 == 0);
}

bool isBlank(const std::string& value)
{
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char character)
        {
            return std::isspace(character) != 0;
        }
    );
}

std::uint64_t readUnsigned(
    const Json& object,
    const char* field,
    std::uint64_t maximum
)
{
    const auto& value = object.at(field);

    std::uint64_t number = 0;

    if (value.is_number_unsigned())
    {
        number = value.get<std::uint64_t>();
    }
    else if (value.is_number_integer())
    {
        const auto signedNumber =
            value.get<std::int64_t>();

        if (signedNumber <= 0)
        {
            throw std::runtime_error(
                std::string("Invalid field: ") + field
            );
        }

        number = static_cast<std::uint64_t>(
            signedNumber
        );
    }
    else
    {
        throw std::runtime_error(
            std::string("Invalid field: ") + field
        );
    }

    if (number == 0 || number > maximum)
    {
        throw std::runtime_error(
            std::string("Field out of range: ") + field
        );
    }

    return number;
}

std::string readString(
    const Json& object,
    const char* field,
    bool allowEmpty = false
)
{
    const auto& value = object.at(field);

    if (!value.is_string())
    {
        throw std::runtime_error(
            std::string("Invalid string: ") + field
        );
    }

    auto result = value.get<std::string>();

    if (!allowEmpty && isBlank(result))
    {
        throw std::runtime_error(
            std::string("Empty string: ") + field
        );
    }

    return result;
}

std::int64_t parseUtcTimestamp(
    const std::string& value
)
{
    if (
        value.size() != 24 ||
        value[10] != 'T' ||
        value[13] != ':' ||
        value[16] != ':' ||
        value[19] != '.' ||
        value[23] != 'Z' ||
        !OttRestClient::isValidDate(value.substr(0, 10)) ||
        !isDigits(value, 11, 2) ||
        !isDigits(value, 14, 2) ||
        !isDigits(value, 17, 2) ||
        !isDigits(value, 20, 3)
    )
    {
        throw std::runtime_error(
            "Invalid UTC timestamp: " + value
        );
    }

    const int hour = std::stoi(value.substr(11, 2));
    const int minute = std::stoi(value.substr(14, 2));
    const int second = std::stoi(value.substr(17, 2));

    if (hour > 23 || minute > 59 || second > 59)
    {
        throw std::runtime_error(
            "Invalid timestamp time"
        );
    }

    std::tm time{};

    time.tm_year =
        std::stoi(value.substr(0, 4)) - 1900;

    time.tm_mon =
        std::stoi(value.substr(5, 2)) - 1;

    time.tm_mday =
        std::stoi(value.substr(8, 2));

    time.tm_hour = hour;
    time.tm_min = minute;
    time.tm_sec = second;
    time.tm_isdst = 0;

    const auto seconds = timegm(&time);

    return static_cast<std::int64_t>(seconds) * 1000 +
        std::stoi(value.substr(20, 3));
}

} // namespace

OttRestClient::OttRestClient(
    const OttRestConfig& config
)
    : config_(config)
{
    if (
        config_.baseUrl.empty() ||
        config_.baseUrl.back() == '/' ||
        config_.connectTimeoutMs <= 0 ||
        config_.requestTimeoutMs <= 0 ||
        config_.connectTimeoutMs > config_.requestTimeoutMs ||
        config_.maxResponseBytes == 0
    )
    {
        throw std::invalid_argument(
            "Invalid OTT REST configuration"
        );
    }

    if (!curlRuntime().available())
    {
        throw std::runtime_error(
            "Failed to initialize libcurl"
        );
    }
}

OttRestResult<std::string> OttRestClient::get(
    const std::string& path
) const
{
    using CurlHandle = std::unique_ptr<
        CURL,
        decltype(&curl_easy_cleanup)
    >;

    CurlHandle handle(
        curl_easy_init(),
        &curl_easy_cleanup
    );

    if (!handle)
    {
        return makeError<std::string>(
            OttRestStatus::InternalError,
            "Failed to create CURL handle"
        );
    }

    const std::string url = config_.baseUrl + path;

    ResponseBuffer response;

    response.limit = config_.maxResponseBytes;

    char errorBuffer[CURL_ERROR_SIZE]{};

    CURL* curl = handle.get();

    if (
        curl_easy_setopt(
            curl, CURLOPT_URL, url.c_str()
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_HTTPGET, 1L
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_CONNECTTIMEOUT_MS,
            config_.connectTimeoutMs
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_TIMEOUT_MS,
            config_.requestTimeoutMs
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_NOSIGNAL, 1L
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_FOLLOWLOCATION, 0L
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_PROTOCOLS_STR, "http,https"
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_PROXY, ""
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_WRITEFUNCTION,
            &writeResponse
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_WRITEDATA, &response
        ) != CURLE_OK ||

        curl_easy_setopt(
            curl, CURLOPT_ERRORBUFFER, errorBuffer
        ) != CURLE_OK
    )
    {
        return makeError<std::string>(
            OttRestStatus::InternalError,
            "Failed to configure CURL request"
        );
    }

    const CURLcode code = curl_easy_perform(curl);

    long httpStatus = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &httpStatus
    );

    if (response.overflow)
    {
        return makeError<std::string>(
            OttRestStatus::InvalidResponse,
            "OTT response exceeds size limit",
            httpStatus
        );
    }

    if (response.writeFailed)
    {
        return makeError<std::string>(
            OttRestStatus::InternalError,
            "Failed to store OTT response",
            httpStatus
        );
    }

    if (code != CURLE_OK)
    {
        const auto status =
            code == CURLE_OPERATION_TIMEDOUT
                ? OttRestStatus::Timeout
                : OttRestStatus::ConnectionError;

        return makeError<std::string>(
            status,
            errorBuffer[0] != '\0'
                ? errorBuffer
                : curl_easy_strerror(code),
            httpStatus
        );
    }

    if (httpStatus != 200)
    {
        return makeError<std::string>(
            OttRestStatus::HttpError,
            "OTT returned HTTP " +
                std::to_string(httpStatus),
            httpStatus
        );
    }

    OttRestResult<std::string> result;

    result.status = OttRestStatus::Success;
    result.httpStatus = httpStatus;
    result.data = std::move(response.data);

    return result;
}

OttRestResult<std::vector<TvService>>
OttRestClient::fetchServices() const
{
    using Services = std::vector<TvService>;

    const auto response = get("/ott/v1/services");

    if (!response.success())
    {
        return forwardError<Services>(response);
    }

    try
    {
        const auto document = Json::parse(response.data);

        if (!document.is_array() || document.empty())
        {
            throw std::runtime_error(
                "Invalid or empty service catalog"
            );
        }

        Services services;

        for (const auto& item : document)
        {
            TvService service;

            service.serviceId =
                static_cast<std::uint16_t>(
                    readUnsigned(item, "serviceId", 65535)
                );

            service.channelNumber =
                static_cast<unsigned int>(
                    readUnsigned(
                        item,
                        "channelNumber",
                        std::numeric_limits<unsigned int>::max()
                    )
                );

            service.name = readString(item, "name");

            service.description =
                readString(item, "description", true);

            const auto& profile =
                item.at("streamProfile");

            if (
                readString(profile, "protocol") != "RTP" ||
                readString(profile, "container") != "MPEG-TS" ||
                readString(profile, "videoCodec") != "H.264" ||
                readString(profile, "audioCodec") != "AAC"
            )
            {
                throw std::runtime_error(
                    "Unsupported stream profile"
                );
            }

            service.rtpAddress =
                readString(profile, "rtpAddress");

            service.rtpPort =
                static_cast<std::uint16_t>(
                    readUnsigned(profile, "rtpPort", 65535)
                );

            services.push_back(std::move(service));
        }

        const ServiceManager manager(
            std::move(services)
        );

        OttRestResult<Services> result;

        result.status = OttRestStatus::Success;
        result.httpStatus = response.httpStatus;
        result.data = manager.services();

        return result;
    }
    catch (const std::exception& error)
    {
        return makeError<Services>(
            OttRestStatus::InvalidResponse,
            error.what(),
            response.httpStatus
        );
    }
}

OttRestResult<std::vector<EpgEvent>>
OttRestClient::fetchEpg(
    std::uint16_t serviceId,
    const std::string& date
) const
{
    using Events = std::vector<EpgEvent>;

    if (serviceId == 0 || !isValidDate(date))
    {
        return makeError<Events>(
            OttRestStatus::InvalidRequest,
            "Invalid service ID or EPG date"
        );
    }

    const auto response = get(
        "/ott/v1/epg?serviceId=" +
        std::to_string(serviceId) +
        "&date=" + date
    );

    if (!response.success())
    {
        return forwardError<Events>(response);
    }

    try
    {
        const auto document = Json::parse(response.data);

        if (!document.is_array())
        {
            throw std::runtime_error(
                "EPG response must be an array"
            );
        }

        const auto dayStart = parseUtcTimestamp(
            date + "T00:00:00.000Z"
        );

        const auto dayEnd =
            dayStart + 24LL * 60LL * 60LL * 1000LL;

        Events events;

        std::set<std::string> eventIds;

        for (const auto& item : document)
        {
            EpgEvent event;

            event.eventId = readString(item, "eventId");

            event.serviceId =
                static_cast<std::uint16_t>(
                    readUnsigned(item, "serviceId", 65535)
                );

            if (event.serviceId != serviceId)
            {
                throw std::runtime_error(
                    "Unexpected EPG service ID"
                );
            }

            if (!eventIds.insert(event.eventId).second)
            {
                throw std::runtime_error(
                    "Duplicate EPG event ID"
                );
            }

            event.title = readString(item, "title");

            event.description =
                readString(item, "description", true);

            event.startTime =
                readString(item, "startTime");

            event.endTime =
                readString(item, "endTime");

            event.startTimeMs =
                parseUtcTimestamp(event.startTime);

            event.endTimeMs =
                parseUtcTimestamp(event.endTime);

            if (
                event.endTimeMs <= event.startTimeMs ||
                event.startTimeMs < dayStart ||
                event.endTimeMs > dayEnd
            )
            {
                throw std::runtime_error(
                    "Invalid EPG event interval"
                );
            }

            events.push_back(std::move(event));
        }

        std::sort(
            events.begin(),
            events.end(),
            [](const EpgEvent& first,
               const EpgEvent& second)
            {
                return first.startTimeMs <
                    second.startTimeMs;
            }
        );

        for (std::size_t i = 1; i < events.size(); ++i)
        {
            if (
                events[i].startTimeMs <
                events[i - 1].endTimeMs
            )
            {
                throw std::runtime_error(
                    "Overlapping EPG events"
                );
            }
        }

        OttRestResult<Events> result;

        result.status = OttRestStatus::Success;
        result.httpStatus = response.httpStatus;
        result.data = std::move(events);

        return result;
    }
    catch (const std::exception& error)
    {
        return makeError<Events>(
            OttRestStatus::InvalidResponse,
            error.what(),
            response.httpStatus
        );
    }
}

bool OttRestClient::isValidDate(
    const std::string& date
)
{
    if (
        date.size() != 10 ||
        date[4] != '-' ||
        date[7] != '-' ||
        !isDigits(date, 0, 4) ||
        !isDigits(date, 5, 2) ||
        !isDigits(date, 8, 2)
    )
    {
        return false;
    }

    const int year = std::stoi(date.substr(0, 4));
    const int month = std::stoi(date.substr(5, 2));
    const int day = std::stoi(date.substr(8, 2));

    if (
        year < 1 ||
        year > 9999 ||
        month < 1 ||
        month > 12 ||
        day < 1
    )
    {
        return false;
    }

    constexpr int daysPerMonth[] =
    {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    int maximum = daysPerMonth[month - 1];

    if (month == 2 && isLeapYear(year))
    {
        ++maximum;
    }

    return day <= maximum;
}

std::string OttRestClient::currentUtcDate()
{
    const std::time_t now = std::time(nullptr);

    std::tm utc{};

    if (gmtime_r(&now, &utc) == nullptr)
    {
        throw std::runtime_error(
            "Failed to obtain UTC date"
        );
    }

    char buffer[11]{};

    if (
        std::strftime(
            buffer,
            sizeof(buffer),
            "%Y-%m-%d",
            &utc
        ) == 0
    )
    {
        throw std::runtime_error(
            "Failed to format UTC date"
        );
    }

    return buffer;
}
