#include "HeadendController.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <spawn.h>
#include <system_error>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

HeadendController::HeadendController(
    const std::filesystem::path& scriptPath
)
    : scriptPath_(scriptPath)
{
}

HeadendController::~HeadendController()
{
    stop();
}

bool HeadendController::start(
    const TvService& service
)
{
    if (!stop())
    {
        return false;
    }

    lastError_.clear();

    if (service.serviceId == 0 ||
        service.rtpPort == 0 ||
        service.rtpAddress.empty())
    {
        lastError_ = "Invalid RTP service";

        return false;
    }

    std::error_code error;

    if (!std::filesystem::is_regular_file(
            scriptPath_,
            error
        ))
    {
        lastError_ =
            "Headend script not found: " +
            scriptPath_.string();

        return false;
    }

    std::string script = scriptPath_.string();

    std::string serviceId =
        std::to_string(service.serviceId);

    std::vector<std::string> environment;

    for (char** entry = environ;
         *entry != nullptr;
         ++entry)
    {
        std::string value(*entry);

        if (value.rfind("RTP_IP=", 0) == 0 ||
            value.rfind("RTP_PORT=", 0) == 0)
        {
            continue;
        }

        environment.push_back(value);
    }

    environment.push_back(
        "RTP_IP=" + service.rtpAddress
    );

    environment.push_back(
        "RTP_PORT=" +
        std::to_string(service.rtpPort)
    );

    std::vector<char*> environmentPointers;

    for (auto& value : environment)
    {
        environmentPointers.push_back(
            value.data()
        );
    }

    environmentPointers.push_back(nullptr);

    char executable[] = "/bin/bash";

    char* arguments[] =
    {
        executable,
        script.data(),
        serviceId.data(),
        nullptr
    };

    posix_spawnattr_t attributes;

    int result =
        ::posix_spawnattr_init(&attributes);

    if (result != 0)
    {
        lastError_ =
            "Failed to initialize spawn attributes";

        return false;
    }

    sigset_t defaultSignals;
    sigset_t signalMask;

    ::sigemptyset(&defaultSignals);
    ::sigaddset(&defaultSignals, SIGINT);
    ::sigaddset(&defaultSignals, SIGTERM);
    ::sigaddset(&defaultSignals, SIGPIPE);

    ::sigemptyset(&signalMask);

    result = ::posix_spawnattr_setpgroup(
        &attributes,
        0
    );

    if (result == 0)
    {
        result = ::posix_spawnattr_setsigdefault(
            &attributes,
            &defaultSignals
        );
    }

    if (result == 0)
    {
        result = ::posix_spawnattr_setsigmask(
            &attributes,
            &signalMask
        );
    }

    if (result == 0)
    {
        result = ::posix_spawnattr_setflags(
            &attributes,
            POSIX_SPAWN_SETPGROUP |
            POSIX_SPAWN_SETSIGDEF |
            POSIX_SPAWN_SETSIGMASK
        );
    }

    pid_t childPid = -1;

    if (result == 0)
    {
        result = ::posix_spawn(
            &childPid,
            executable,
            nullptr,
            &attributes,
            arguments,
            environmentPointers.data()
        );
    }

    ::posix_spawnattr_destroy(&attributes);

    if (result != 0)
    {
        lastError_ =
            "Failed to start Headend: " +
            std::error_code(
                result,
                std::generic_category()
            ).message();

        return false;
    }

    processId_ = childPid;

    std::cout
        << "[INFO] Headend started"
        << ", service: " << service.serviceId
        << ", PID: " << processId_
        << ", destination: "
        << service.rtpAddress
        << ':' << service.rtpPort
        << '\n';

    return true;
}

bool HeadendController::isRunning()
{
    if (processId_ <= 0)
    {
        return false;
    }

    int status = 0;

    pid_t result;

    do
    {
        result = ::waitpid(
            processId_,
            &status,
            WNOHANG
        );
    }
    while (result < 0 && errno == EINTR);

    if (result == 0)
    {
        return true;
    }

    if (result == processId_)
    {
        if (WIFEXITED(status))
        {
            lastError_ =
                "Headend exited with code " +
                std::to_string(
                    WEXITSTATUS(status)
                );
        }
        else if (WIFSIGNALED(status))
        {
            lastError_ =
                "Headend terminated by signal " +
                std::to_string(
                    WTERMSIG(status)
                );
        }
        else
        {
            lastError_ =
                "Headend process stopped";
        }

        processId_ = -1;

        return false;
    }

    const int waitError = errno;

    lastError_ =
        "Headend waitpid failed: " +
        std::error_code(
            waitError,
            std::generic_category()
        ).message();

    if (waitError == ECHILD)
    {
        processId_ = -1;
    }

    return false;
}

bool HeadendController::stop()
{
    if (processId_ <= 0)
    {
        return true;
    }

    if (!isRunning())
    {
        return processId_ <= 0;
    }

    const pid_t childPid = processId_;

    std::cout
        << "[INFO] Stopping Headend PID: "
        << childPid << '\n';

    if (::kill(childPid, SIGTERM) < 0 &&
        errno != ESRCH)
    {
        lastError_ =
            "Failed to terminate Headend";

        return false;
    }

    for (int i = 0; i < 30; ++i)
    {
        if (!isRunning())
        {
            if (processId_ > 0)
            {
                return false;
            }

            lastError_.clear();

            std::cout
                << "[INFO] Headend stopped\n";

            return true;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    std::cerr
        << "[WARNING] Headend shutdown timeout, "
        << "sending SIGKILL\n";

    if (::kill(childPid, SIGKILL) < 0 &&
        errno != ESRCH)
    {
        lastError_ =
            "Failed to kill Headend";

        return false;
    }

    pid_t result;

    do
    {
        result = ::waitpid(
            childPid,
            nullptr,
            0
        );
    }
    while (result < 0 && errno == EINTR);

    if (result < 0 && errno != ECHILD)
    {
        lastError_ =
            "Failed to reap Headend process";

        return false;
    }

    processId_ = -1;

    lastError_.clear();

    std::cout
        << "[INFO] Headend stopped forcibly\n";

    return true;
}

pid_t HeadendController::processId() const noexcept
{
    return processId_;
}

std::string HeadendController::lastError() const
{
    return lastError_;
}
