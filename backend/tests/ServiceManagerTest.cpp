#include "ServiceManager.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

void require(
    bool condition,
    const char* message
)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void testDefaultCatalog()
{
    ServiceManager manager;

    require(
        manager.serviceCount() == 3,
        "Expected three services"
    );

    require(
        !manager.empty(),
        "Catalog should not be empty"
    );

    require(
        manager.firstService().value().serviceId == 1,
        "Incorrect first service"
    );
}

void testFindById()
{
    ServiceManager manager;

    require(
        manager.findById(2).value().serviceId == 2,
        "Incorrect service ID"
    );

    require(
        !manager.findById(99).has_value(),
        "Unknown service should not exist"
    );
}

void testFindByNumber()
{
    ServiceManager manager;

    require(
        manager.findByNumber(3).value().serviceId == 3,
        "Incorrect channel number"
    );

    require(
        !manager.findByNumber(0).has_value(),
        "Channel zero should not exist"
    );

    require(
        !manager.findByNumber(99).has_value(),
        "Unknown channel should not exist"
    );
}

void testNextService()
{
    ServiceManager manager;

    require(
        manager.nextService(1).value().serviceId == 2,
        "Next service after 1 should be 2"
    );

    require(
        manager.nextService(2).value().serviceId == 3,
        "Next service after 2 should be 3"
    );

    require(
        manager.nextService(3).value().serviceId == 1,
        "Next service should wrap around"
    );
}

void testPreviousService()
{
    ServiceManager manager;

    require(
        manager.previousService(3).value().serviceId == 2,
        "Previous service after 3 should be 2"
    );

    require(
        manager.previousService(2).value().serviceId == 1,
        "Previous service after 2 should be 1"
    );

    require(
        manager.previousService(1).value().serviceId == 3,
        "Previous service should wrap around"
    );
}

void testInvalidCurrentService()
{
    ServiceManager manager;

    require(
        !manager.nextService(99).has_value(),
        "Invalid next service"
    );

    require(
        !manager.previousService(99).has_value(),
        "Invalid previous service"
    );
}

void testEmptyCatalog()
{
    ServiceManager manager(
        std::vector<TvService>{}
    );

    require(
        manager.empty(),
        "Expected empty catalog"
    );

    require(
        manager.serviceCount() == 0,
        "Expected zero services"
    );

    require(
        !manager.firstService().has_value(),
        "Empty catalog has no first service"
    );

    require(
        !manager.nextService(1).has_value(),
        "Empty catalog has no next service"
    );

    require(
        !manager.previousService(1).has_value(),
        "Empty catalog has no previous service"
    );
}

void testSingleService()
{
    ServiceManager manager(
        {
            {10, 5, "Single channel", ""}
        }
    );

    require(
        manager.nextService(10).value().serviceId == 10,
        "Single service next failed"
    );

    require(
        manager.previousService(10).value().serviceId == 10,
        "Single service previous failed"
    );
}

void testCustomCatalog()
{
    ServiceManager manager(
        {
            {30, 7, "Channel C", ""},
            {10, 1, "Channel A", ""},
            {20, 4, "Channel B", ""}
        }
    );

    require(
        manager.firstService().value().serviceId == 10,
        "Catalog sorting failed"
    );

    require(
        manager.findByNumber(4).value().serviceId == 20,
        "Channel number differs from service ID"
    );

    require(
        manager.nextService(10).value().serviceId == 20,
        "Non-contiguous channel navigation failed"
    );

    require(
        manager.nextService(30).value().serviceId == 10,
        "Custom catalog wrap around failed"
    );
}

void testInvalidCatalog()
{
    const auto expectInvalid =
        [](std::vector<TvService> services)
        {
            bool rejected = false;

            try
            {
                ServiceManager manager(services);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }

            require(
                rejected,
                "Invalid catalog was accepted"
            );
        };

    expectInvalid(
        {
            {1, 1, "A", ""},
            {1, 2, "B", ""}
        }
    );

    expectInvalid(
        {
            {1, 1, "A", ""},
            {2, 1, "B", ""}
        }
    );

    expectInvalid(
        {
            {0, 1, "A", ""}
        }
    );

    expectInvalid(
        {
            {1, 0, "A", ""}
        }
    );

    expectInvalid(
        {
            {1, 1, "", ""}
        }
    );

    expectInvalid(
        {
            {1, 1, "A", "", "", 5004}
        }
    );

    expectInvalid(
        {
            {1, 1, "A", "", "127.0.0.1", 0}
        }
    );
}

}

int main()
{
    struct TestCase
    {
        const char* name;
        void (*function)();
    };

    const TestCase tests[] =
    {
        {"Default catalog", testDefaultCatalog},
        {"Find by ID", testFindById},
        {"Find by number", testFindByNumber},
        {"Next service", testNextService},
        {"Previous service", testPreviousService},
        {"Invalid current service", testInvalidCurrentService},
        {"Empty catalog", testEmptyCatalog},
        {"Single service", testSingleService},
        {"Custom catalog", testCustomCatalog},
        {"Invalid catalog", testInvalidCatalog}
    };

    unsigned int passed = 0;
    unsigned int failed = 0;

    for (const auto& test : tests)
    {
        try
        {
            test.function();

            std::cout
                << "[PASS] "
                << test.name
                << '\n';

            ++passed;
        }
        catch (const std::exception& error)
        {
            std::cerr
                << "[FAIL] "
                << test.name
                << ": "
                << error.what()
                << '\n';

            ++failed;
        }
    }

    std::cout
        << "\nPassed: " << passed
        << "\nFailed: " << failed
        << '\n';

    return failed == 0 ? 0 : 1;
}
