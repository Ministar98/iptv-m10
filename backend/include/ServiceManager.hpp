#pragma once

#include "TvService.hpp"

#include <cstdint>
#include <optional>
#include <vector>

class ServiceManager
{
public:
    ServiceManager();

    explicit ServiceManager(
        std::vector<TvService> services
    );

    const std::vector<TvService>& services() const;

    std::size_t serviceCount() const;

    bool empty() const;

    std::optional<TvService> firstService() const;

    std::optional<TvService> findById(
        std::uint16_t serviceId
    ) const;

    std::optional<TvService> findByNumber(
        unsigned int channelNumber
    ) const;

    std::optional<TvService> nextService(
        std::uint16_t currentServiceId
    ) const;

    std::optional<TvService> previousService(
        std::uint16_t currentServiceId
    ) const;

private:
    std::vector<TvService> services_;

    std::optional<std::size_t> findIndexById(
        std::uint16_t serviceId
    ) const;

    static void validateServices(
        const std::vector<TvService>& services
    );
};
