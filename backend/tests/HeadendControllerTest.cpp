#include "HeadendController.hpp"
#include "ServiceManager.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

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

}

int main()
{
    try
    {
        ServiceManager services;

        HeadendController headend;

        require(
            !headend.isRunning(),
            "Headend should initially be stopped"
        );

        pid_t previousPid = -1;

        for (const auto& service :
             services.services())
        {
            TvService testService = service;

            testService.rtpPort = 55004;

            require(
                headend.start(testService),
                "Headend startup failed"
            );

            std::this_thread::sleep_for(
                std::chrono::seconds(2)
            );

            require(
                headend.isRunning(),
                "Headend stopped unexpectedly: " +
                headend.lastError()
            );

            require(
                headend.processId() > 0,
                "Invalid Headend PID"
            );

            require(
                headend.processId() != previousPid,
                "Headend process was not replaced"
            );

            previousPid =
                headend.processId();

            std::cout
                << "[PASS] Service "
                << service.serviceId
                << " started successfully\n";
        }

        require(
            headend.stop(),
            "Headend shutdown failed"
        );

        require(
            !headend.isRunning(),
            "Headend still running"
        );

        require(
            headend.processId() == -1,
            "Headend PID was not cleared"
        );

        std::cout
            << "[PASS] Headend shutdown\n";

        require(
            headend.stop(),
            "Repeated shutdown failed"
        );

        std::cout
            << "[PASS] Repeated shutdown\n";

        HeadendController invalidHeadend(
            "nonexistent_script.sh"
        );

        require(
            !invalidHeadend.start(
                services.firstService().value()
            ),
            "Missing script should be rejected"
        );

        std::cout
            << "[PASS] Missing script detection\n";

        std::cout
            << "\nAll Headend tests passed\n";

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
