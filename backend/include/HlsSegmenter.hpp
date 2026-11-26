#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <sys/types.h>

struct HlsConfig
{
    std::filesystem::path outputDirectory =
        "runtime/hls";

    unsigned int segmentDurationSeconds = 2;

    unsigned int playlistSize = 60;

    unsigned int deleteThreshold = 2;

    std::string playlistFilename =
        "index.m3u8";

    bool deleteSegments = true;
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
