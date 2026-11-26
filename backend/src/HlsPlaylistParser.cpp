#include "HlsPlaylistParser.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <utility>

namespace
{
    std::string trim(
        const std::string& value
    )
    {
        const auto first =
            value.find_first_not_of(" \t\r\n");

        if (first == std::string::npos)
        {
            return {};
        }

        const auto last =
            value.find_last_not_of(" \t\r\n");

        return value.substr(
            first,
            last - first + 1
        );
    }

    bool startsWith(
        const std::string& value,
        const std::string& prefix
    )
    {
        return value.compare(
            0,
            prefix.size(),
            prefix
        ) == 0;
    }

    bool parseUnsigned(
        std::string_view value,
        std::uint64_t& result
    )
    {
        if (value.empty())
        {
            return false;
        }

        std::uint64_t parsed = 0;

        const auto conversion =
            std::from_chars(
                value.data(),
                value.data() + value.size(),
                parsed
            );

        if (conversion.ec != std::errc{} ||
            conversion.ptr !=
                value.data() + value.size())
        {
            return false;
        }

        result = parsed;

        return true;
    }

    bool parseDuration(
        const std::string& value,
        std::uint64_t& durationMs
    )
    {
        const auto comma = value.find(',');

        if (comma == std::string::npos)
        {
            return false;
        }

        const std::string duration =
            trim(value.substr(0, comma));

        if (duration.empty())
        {
            return false;
        }

        std::istringstream input(duration);

        input.imbue(std::locale::classic());

        double seconds = 0.0;

        input >> std::noskipws >> seconds;

        if (!input ||
            input.peek() !=
                std::char_traits<char>::eof() ||
            !std::isfinite(seconds) ||
            seconds <= 0.0 ||
            seconds > 86400.0)
        {
            return false;
        }

        const auto milliseconds =
            std::llround(seconds * 1000.0);

        if (milliseconds <= 0)
        {
            return false;
        }

        durationMs =
            static_cast<std::uint64_t>(
                milliseconds
            );

        return true;
    }

    bool validSegmentName(
        const std::string& name
    )
    {
        if (name.empty())
        {
            return false;
        }

        // Accept only local MPEG-TS filenames.

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

        const std::filesystem::path path(name);

        return !path.has_parent_path() &&
               path.extension() == ".ts";
    }
}

bool HlsPlaylistParser::parse(
    const std::string& content,
    HlsPlaylist& playlist
)
{
    HlsPlaylist parsed{};

    std::istringstream input(content);

    std::string line;

    bool headerFound = false;

    bool mediaSequenceFound = false;

    bool targetDurationFound = false;

    bool versionFound = false;

    bool durationPending = false;

    bool discontinuityPending = false;

    std::uint64_t pendingDurationMs = 0;

    while (std::getline(input, line))
    {
        line = trim(line);

        if (line.empty())
        {
            continue;
        }

        if (!headerFound)
        {
            if (line != "#EXTM3U")
            {
                return false;
            }

            headerFound = true;

            continue;
        }

        if (parsed.endList)
        {
            return false;
        }

        if (startsWith(
                line,
                "#EXT-X-VERSION:"
            ))
        {
            std::uint64_t value = 0;

            if (versionFound ||
                !parseUnsigned(
                    line.substr(15),
                    value
                ) ||
                value == 0 ||
                value >
                    std::numeric_limits<
                        unsigned int
                    >::max())
            {
                return false;
            }

            parsed.version =
                static_cast<unsigned int>(
                    value
                );

            versionFound = true;

            continue;
        }

        if (startsWith(
                line,
                "#EXT-X-TARGETDURATION:"
            ))
        {
            std::uint64_t value = 0;

            if (targetDurationFound ||
                !parseUnsigned(
                    line.substr(22),
                    value
                ) ||
                value == 0 ||
                value >
                    std::numeric_limits<
                        unsigned int
                    >::max())
            {
                return false;
            }

            parsed.targetDurationSeconds =
                static_cast<unsigned int>(
                    value
                );

            targetDurationFound = true;

            continue;
        }

        if (startsWith(
                line,
                "#EXT-X-MEDIA-SEQUENCE:"
            ))
        {
            if (mediaSequenceFound ||
                !parsed.segments.empty() ||
                !parseUnsigned(
                    line.substr(22),
                    parsed.mediaSequence
                ))
            {
                return false;
            }

            mediaSequenceFound = true;

            continue;
        }

        if (line ==
            "#EXT-X-INDEPENDENT-SEGMENTS")
        {
            parsed.independentSegments = true;

            continue;
        }

        if (line == "#EXT-X-DISCONTINUITY")
        {
            discontinuityPending = true;

            continue;
        }

        if (line == "#EXT-X-ENDLIST")
        {
            if (durationPending ||
                discontinuityPending)
            {
                return false;
            }

            parsed.endList = true;

            continue;
        }

        if (startsWith(line, "#EXTINF:"))
        {
            if (durationPending ||
                !parseDuration(
                    line.substr(8),
                    pendingDurationMs
                ))
            {
                return false;
            }

            durationPending = true;

            continue;
        }

        // Unsupported playlist formats.

        if (startsWith(
                line,
                "#EXT-X-STREAM-INF:"
            ) ||
            startsWith(
                line,
                "#EXT-X-I-FRAME-STREAM-INF:"
            ) ||
            startsWith(
                line,
                "#EXT-X-BYTERANGE:"
            ) ||
            startsWith(
                line,
                "#EXT-X-KEY:"
            ) ||
            startsWith(
                line,
                "#EXT-X-MAP:"
            ) ||
            startsWith(
                line,
                "#EXT-X-GAP"
            ))
        {
            return false;
        }

        // Ignore other tags and comments.

        if (line.front() == '#')
        {
            continue;
        }

        if (!durationPending ||
            !validSegmentName(line))
        {
            return false;
        }

        if (parsed.segments.size() >
            std::numeric_limits<
                std::uint64_t
            >::max() -
            parsed.mediaSequence)
        {
            return false;
        }

        HlsPlaylistSegment segment{};

        segment.sequenceNumber =
            parsed.mediaSequence +
            parsed.segments.size();

        segment.fileName = line;

        segment.durationMs =
            pendingDurationMs;

        segment.discontinuity =
            discontinuityPending;

        parsed.segments.push_back(
            std::move(segment)
        );

        durationPending = false;

        discontinuityPending = false;

        pendingDurationMs = 0;
    }

    if (!headerFound ||
        !targetDurationFound ||
        durationPending ||
        discontinuityPending ||
        parsed.segments.empty())
    {
        return false;
    }

    playlist = std::move(parsed);

    return true;
}

bool HlsPlaylistParser::parseFile(
    const std::filesystem::path& filePath,
    HlsPlaylist& playlist
)
{
    std::ifstream input(
        filePath,
        std::ios::binary
    );

    if (!input)
    {
        return false;
    }

    std::ostringstream content;

    content << input.rdbuf();

    if (input.bad() || content.bad())
    {
        return false;
    }

    return parse(
        content.str(),
        playlist
    );
}
