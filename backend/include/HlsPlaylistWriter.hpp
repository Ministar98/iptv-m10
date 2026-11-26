#pragma once

#include "CircularDiskBuffer.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

class HlsPlaylistWriter
{
public:
    static bool write(
        const std::filesystem::path& outputPath,
        const std::vector<SegmentMetadata>& segments,
        unsigned int targetDurationSeconds,
        bool independentSegments = true,
        bool endList = false,
        std::uint64_t discontinuitySequence = 0
    );
};
