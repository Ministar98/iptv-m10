#include "ServiceManager.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

ServiceManager::ServiceManager()
    : ServiceManager(
        {
            {
                1,
                1,
                "Channel 1",
                "IPTV test service 1",
                "127.0.0.1",
                5004
            },
            {
                2,
                2,
                "Channel 2",
                "IPTV test service 2",
                "127.0.0.1",
                5004
            },
            {
                3,
                3,
                "Channel 3",
                "IPTV test service 3",
                "127.0.0.1",
                5004
            }
        }
    )
{
}

ServiceManager::ServiceManager(
    std::vector<TvService> services
)
    : services_(std::move(services))
{
    validateServices(services_);

    std::sort(
        services_.begin(),
        services_.end(),
        [](const TvService& first,
           const TvService& second)
        {
            return first.channelNumber <
                   second.channelNumber;
        }
    );
}

void ServiceManager::validateServices(
    const std::vector<TvService>& services
)
{
    std::unordered_set<std::uint16_t> serviceIds;

    std::unordered_set<unsigned int> channelNumbers;

    for (const auto& service : services)
    {
        if (service.serviceId == 0 ||
            service.channelNumber == 0 ||
            service.name.empty() ||
            service.rtpAddress.empty() ||
            service.rtpPort == 0)
        {
            throw std::invalid_argument(
                "Invalid TV service"
            );
        }

        if (!serviceIds.insert(
                service.serviceId
            ).second)
        {
            throw std::invalid_argument(
                "Duplicate service ID"
            );
        }

        if (!channelNumbers.insert(
                service.channelNumber
            ).second)
        {
            throw std::invalid_argument(
                "Duplicate channel number"
            );
        }
    }
}

const std::vector<TvService>&
ServiceManager::services() const
{
    return services_;
}

std::size_t ServiceManager::serviceCount() const
{
    return services_.size();
}

bool ServiceManager::empty() const
{
    return services_.empty();
}

std::optional<TvService>
ServiceManager::firstService() const
{
    if (services_.empty())
    {
        return std::nullopt;
    }

    return services_.front();
}

std::optional<std::size_t>
ServiceManager::findIndexById(
    std::uint16_t serviceId
) const
{
    for (std::size_t i = 0;
         i < services_.size();
         ++i)
    {
        if (services_[i].serviceId == serviceId)
        {
            return i;
        }
    }

    return std::nullopt;
}

std::optional<TvService>
ServiceManager::findById(
    std::uint16_t serviceId
) const
{
    const auto index =
        findIndexById(serviceId);

    if (!index.has_value())
    {
        return std::nullopt;
    }

    return services_[*index];
}

std::optional<TvService>
ServiceManager::findByNumber(
    unsigned int channelNumber
) const
{
    const auto iterator = std::find_if(
        services_.begin(),
        services_.end(),
        [channelNumber](const TvService& service)
        {
            return service.channelNumber ==
                   channelNumber;
        }
    );

    if (iterator == services_.end())
    {
        return std::nullopt;
    }

    return *iterator;
}

std::optional<TvService>
ServiceManager::nextService(
    std::uint16_t currentServiceId
) const
{
    const auto index =
        findIndexById(currentServiceId);

    if (!index.has_value())
    {
        return std::nullopt;
    }

    const std::size_t nextIndex =
        (*index + 1) % services_.size();

    return services_[nextIndex];
}

std::optional<TvService>
ServiceManager::previousService(
    std::uint16_t currentServiceId
) const
{
    const auto index =
        findIndexById(currentServiceId);

    if (!index.has_value())
    {
        return std::nullopt;
    }

    const std::size_t previousIndex =
        (*index == 0)
            ? services_.size() - 1
            : *index - 1;

    return services_[previousIndex];
}
