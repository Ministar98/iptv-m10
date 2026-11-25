#include "HlsSegmenter.hpp"

#include <array>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    std::signal(SIGPIPE, SIG_IGN);

    const std::filesystem::path inputPath =
        argc > 1
            ? argv[1]
            : "media/channel1.ts";

    const std::filesystem::path outputPath =
        "runtime/hls_test";

    std::cout
        << "IPTV M10 HLS Segmenter Test\n\n";

    std::ifstream input(
        inputPath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "[FAIL] Cannot open input file\n";

        return 1;
    }

    std::cout
        << "[PASS] Input file opened\n";

    std::error_code error;

    std::filesystem::remove_all(
        outputPath,
        error
    );

    if (error)
    {
        std::cerr
            << "[FAIL] Cannot clean test directory\n";

        return 1;
    }

    HlsConfig config{};

    config.outputDirectory = outputPath;

    config.segmentDurationSeconds = 2;

    config.playlistSize = 6;

    config.deleteThreshold = 2;

    HlsSegmenter segmenter;

    if (!segmenter.start(config))
    {
        std::cerr
            << "[FAIL] Cannot start HLS segmenter\n";

        return 1;
    }

    std::cout
        << "[PASS] HLS process started\n";

    if (!segmenter.isRunning())
    {
        std::cerr
            << "[FAIL] HLS process not running\n";

        return 1;
    }

    std::cout
        << "[PASS] HLS process running\n";

    constexpr std::size_t BUFFER_SIZE =
        188 * 7 * 32;

    std::array<
        std::uint8_t,
        BUFFER_SIZE
    > buffer{};

    std::size_t totalBytes = 0;

    while (input)
    {
        input.read(
            reinterpret_cast<char*>(
                buffer.data()
            ),
            buffer.size()
        );

        const std::streamsize bytesRead =
            input.gcount();

        if (bytesRead <= 0)
        {
            break;
        }

        if (!segmenter.write(
                buffer.data(),
                static_cast<std::size_t>(
                    bytesRead
                )
            ))
        {
            std::cerr
                << "[FAIL] MPEG-TS pipe write\n";

            segmenter.stop();

            return 1;
        }

        totalBytes +=
            static_cast<std::size_t>(
                bytesRead
            );
    }

    if (input.bad() || totalBytes == 0)
    {
        std::cerr
            << "[FAIL] Input file read\n";

        segmenter.stop();

        return 1;
    }

    std::cout
        << "[PASS] MPEG-TS data transferred\n";

    std::cout
        << "Transferred bytes: "
        << totalBytes << '\n';

    segmenter.stop();

    if (segmenter.isRunning())
    {
        std::cerr
            << "[FAIL] FFmpeg process still running\n";

        return 1;
    }

    std::cout
        << "[PASS] FFmpeg process stopped\n";

    const auto playlistPath =
        outputPath / "index.m3u8";

    if (!std::filesystem::exists(
            playlistPath
        ))
    {
        std::cerr
            << "[FAIL] Playlist not generated\n";

        return 1;
    }

    std::cout
        << "[PASS] HLS playlist generated\n";

    std::ifstream playlist(playlistPath);

    std::string line;

    std::size_t playlistSegments = 0;

    bool validHeader = false;

    while (std::getline(playlist, line))
    {
        if (line == "#EXTM3U")
        {
            validHeader = true;
        }

        if (!line.empty() &&
            line[0] != '#')
        {
            ++playlistSegments;

            if (!std::filesystem::exists(
                    outputPath / line
                ))
            {
                std::cerr
                    << "[FAIL] Missing segment: "
                    << line << '\n';

                return 1;
            }
        }
    }

    if (!validHeader ||
        playlistSegments == 0 ||
        playlistSegments >
            config.playlistSize)
    {
        std::cerr
            << "[FAIL] Invalid HLS playlist\n";

        return 1;
    }

    std::cout
        << "[PASS] Valid HLS playlist structure\n";

    std::cout
        << "[PASS] All referenced segments exist\n";

    std::size_t diskSegments = 0;

    for (const auto& entry :
         std::filesystem::directory_iterator(
             outputPath
         ))
    {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".ts")
        {
            ++diskSegments;
        }
    }

    std::cout
        << "\nPlaylist segments: "
        << playlistSegments << '\n';

    std::cout
        << "Segments on disk: "
        << diskSegments << '\n';

    if (diskSegments >
        config.playlistSize +
        config.deleteThreshold + 1)
    {
        std::cerr
            << "[FAIL] Too many disk segments\n";

        return 1;
    }

    std::cout
        << "[PASS] Disk segment count bounded\n";

    std::cout
        << "\nResult: PASS\n";

    return 0;
}
