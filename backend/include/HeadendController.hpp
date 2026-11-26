#pragma once

#include "TvService.hpp"

#include <filesystem>
#include <string>

#include <sys/types.h>

class HeadendController
{
public:
    explicit HeadendController(
        const std::filesystem::path& scriptPath =
            "producer/stream_channel.sh"
    );

    ~HeadendController();

    HeadendController(
        const HeadendController&
    ) = delete;

    HeadendController& operator=(
        const HeadendController&
    ) = delete;

    bool start(const TvService& service);

    bool stop();

    bool isRunning();

    pid_t processId() const noexcept;

    std::string lastError() const;

private:
    const std::filesystem::path scriptPath_;

    pid_t processId_ = -1;

    std::string lastError_;
};
