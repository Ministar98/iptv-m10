#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sys/types.h>

struct HlsConfig
{
    std::filesystem::path outputDirectory =
        "runtime/hls";

    unsigned int segmentDurationSeconds = 2;

    unsigned int playlistSize = 6;

    unsigned int deleteThreshold = 2;
};

class HlsSegmenter
{
public:
    HlsSegmenter();

    ~HlsSegmenter();

    HlsSegmenter(const HlsSegmenter&) = delete;

    HlsSegmenter& operator=(
        const HlsSegmenter&
    ) = delete;

    bool start(const HlsConfig& config);

    bool write(
        const std::uint8_t* data,
        std::size_t size
    );

    void stop();

    bool isRunning();

    pid_t processId() const;

private:
    int inputFd_;

    pid_t processId_;

    void closeInput();
};
