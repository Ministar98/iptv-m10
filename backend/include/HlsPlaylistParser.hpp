#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct HlsPlaylistSegment
{
    std::uint64_t sequenceNumber = 0;

    std::filesystem::path fileName;

    std::uint64_t durationMs = 0;

    bool discontinuity = false;
};

struct HlsPlaylist
{
    std::uint64_t mediaSequence = 0;

    unsigned int version = 0;

    unsigned int targetDurationSeconds = 0;

    bool independentSegments = false;

    bool endList = false;

    std::vector<HlsPlaylistSegment> segments;
};

class HlsPlaylistParser
{
public:
    static bool parse(
        const std::string& content,
        HlsPlaylist& playlist
    );

    static bool parseFile(
        const std::filesystem::path& filePath,
        HlsPlaylist& playlist
    );
};
