#include "HlsPlaylistWriter.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    bool validFileName(const std::string& name)
    {
        if (name.empty())
        {
            return false;
        }

        for (const char character : name)
        {
            const bool allowed =
                (character >= 'a' &&
                 character <= 'z') ||
                (character >= 'A' &&
                 character <= 'Z') ||
                (character >= '0' &&
                 character <= '9') ||
                character == '_' ||
                character == '-' ||
                character == '.';

            if (!allowed)
            {
                return false;
            }
        }

        return fs::path(name).extension() == ".ts";
    }
}

bool HlsPlaylistWriter::write(
    const fs::path& outputPath,
    const std::vector<SegmentMetadata>& segments,
    unsigned int targetDurationSeconds,
    bool independentSegments,
    bool endList,
    std::uint64_t discontinuitySequence
)
{
    if (outputPath.empty() ||
        outputPath.extension() != ".m3u8" ||
        segments.empty() ||
        targetDurationSeconds == 0)
    {
        return false;
    }

    std::error_code error;

    const fs::path parentDirectory =
        outputPath.has_parent_path()
            ? outputPath.parent_path()
            : fs::path(".");

    const fs::path storageDirectory =
        fs::canonical(parentDirectory, error);

    if (error)
    {
        return false;
    }

    std::set<std::string> fileNames;

    // Validate all segments before writing.

    for (std::size_t i = 0;
         i < segments.size();
         ++i)
    {
        const auto& segment = segments[i];

        if (segment.durationMs == 0 ||
            segment.durationMs >
                std::numeric_limits<
                    std::uint64_t
                >::max() - segment.startTimeMs)
        {
            return false;
        }

        const std::uint64_t roundedDuration =
            segment.durationMs / 1000 +
            (segment.durationMs % 1000 >= 500
                ? 1
                : 0);

        if (roundedDuration >
            targetDurationSeconds)
        {
            return false;
        }

        const std::string fileName =
            segment.filePath.filename().string();

        if (!validFileName(fileName) ||
            !fileNames.insert(fileName).second)
        {
            return false;
        }

        const auto fileStatus =
            fs::symlink_status(
                segment.filePath,
                error
            );

        if (error ||
            !fs::is_regular_file(fileStatus))
        {
            return false;
        }

        const fs::path canonicalPath =
            fs::canonical(
                segment.filePath,
                error
            );

        if (error ||
            canonicalPath.parent_path() !=
                storageDirectory)
        {
            return false;
        }

        if (i > 0)
        {
            const auto& previous =
                segments[i - 1];

            if (previous.sequenceNumber ==
                    std::numeric_limits<
                        std::uint64_t
                    >::max() ||
                segment.sequenceNumber !=
                    previous.sequenceNumber + 1 ||
                segment.startTimeMs !=
                    previous.endTimeMs())
            {
                return false;
            }
        }
    }

    // Write a temporary playlist first.

    fs::path temporaryPath = outputPath;

    temporaryPath += ".tmp";

    std::ofstream output(
        temporaryPath,
        std::ios::out | std::ios::trunc
    );

    if (!output)
    {
        return false;
    }

    output.imbue(std::locale::classic());

    output
        << "#EXTM3U\n"
        << "#EXT-X-VERSION:6\n"
        << "#EXT-X-TARGETDURATION:"
        << targetDurationSeconds << '\n'
        << "#EXT-X-MEDIA-SEQUENCE:"
        << segments.front().sequenceNumber
        << '\n'
        << "#EXT-X-DISCONTINUITY-SEQUENCE:"
        << discontinuitySequence << '\n';

    if (independentSegments)
    {
        output
            << "#EXT-X-INDEPENDENT-SEGMENTS\n";
    }

    for (const auto& segment : segments)
    {
        if (segment.discontinuity)
        {
            output << "#EXT-X-DISCONTINUITY\n";
        }

        output
            << "#EXTINF:"
            << segment.durationMs / 1000
            << '.'
            << std::setfill('0')
            << std::setw(3)
            << segment.durationMs % 1000
            << ",\n"
            << segment.filePath.filename().string()
            << '\n';
    }

    if (endList)
    {
        output << "#EXT-X-ENDLIST\n";
    }

    output.close();

    if (!output)
    {
        fs::remove(temporaryPath, error);

        return false;
    }

    // Atomic replacement on Linux.

    fs::rename(
        temporaryPath,
        outputPath,
        error
    );

    if (error)
    {
        std::error_code cleanupError;

        fs::remove(
            temporaryPath,
            cleanupError
        );

        return false;
    }

    return true;
}
