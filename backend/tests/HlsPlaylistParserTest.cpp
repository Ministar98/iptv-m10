#include "HlsPlaylistParser.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    int passed = 0;
    int failed = 0;

    void check(
        bool condition,
        const std::string& description
    )
    {
        if (condition)
        {
            ++passed;

            std::cout
                << "[PASS] "
                << description << '\n';
        }
        else
        {
            ++failed;

            std::cerr
                << "[FAIL] "
                << description << '\n';
        }
    }

    const std::string validHeader =
        "#EXTM3U\n"
        "#EXT-X-VERSION:6\n"
        "#EXT-X-TARGETDURATION:2\n"
        "#EXT-X-MEDIA-SEQUENCE:150\n"
        "#EXT-X-INDEPENDENT-SEGMENTS\n";

    const std::string validPlaylist =
        validHeader +
        "#EXTINF:2.000000,\n"
        "segment_000150.ts\n"
        "#EXTINF:1.960000,\n"
        "segment_000151.ts\n"
        "#EXTINF:2.040000,\n"
        "segment_000152.ts\n";

    void expectInvalid(
        const std::string& content,
        const std::string& description
    )
    {
        HlsPlaylist playlist{};

        check(
            !HlsPlaylistParser::parse(
                content,
                playlist
            ),
            description
        );
    }

    void testValidPlaylist()
    {
        HlsPlaylist playlist{};

        const bool parsed =
            HlsPlaylistParser::parse(
                validPlaylist,
                playlist
            );

        check(
            parsed,
            "Valid playlist parsed"
        );

        if (!parsed)
        {
            return;
        }

        check(
            playlist.version == 6,
            "Correct HLS version"
        );

        check(
            playlist.targetDurationSeconds == 2,
            "Correct target duration"
        );

        check(
            playlist.mediaSequence == 150,
            "Correct media sequence"
        );

        check(
            playlist.independentSegments,
            "Independent segments detected"
        );

        check(
            !playlist.endList,
            "Live playlist detected"
        );

        check(
            playlist.segments.size() == 3,
            "Correct segment count"
        );

        if (playlist.segments.size() != 3)
        {
            return;
        }

        const auto& first =
            playlist.segments[0];

        const auto& second =
            playlist.segments[1];

        const auto& third =
            playlist.segments[2];

        check(
            first.sequenceNumber == 150 &&
            second.sequenceNumber == 151 &&
            third.sequenceNumber == 152,
            "Correct segment sequence numbers"
        );

        check(
            first.durationMs == 2000 &&
            second.durationMs == 1960 &&
            third.durationMs == 2040,
            "Correct variable segment durations"
        );

        check(
            first.fileName ==
                "segment_000150.ts" &&
            second.fileName ==
                "segment_000151.ts" &&
            third.fileName ==
                "segment_000152.ts",
            "Correct segment filenames"
        );

        check(
            !first.discontinuity &&
            !second.discontinuity &&
            !third.discontinuity,
            "No unexpected discontinuities"
        );
    }

    void testDiscontinuity()
    {
        const std::string content =
            validHeader +
            "#EXTINF:2.000000,\n"
            "segment_000150.ts\n"
            "#EXT-X-DISCONTINUITY\n"
            "#EXTINF:2.000000,\n"
            "segment_000151.ts\n"
            "#EXTINF:2.000000,\n"
            "segment_000152.ts\n"
            "#EXT-X-ENDLIST\n";

        HlsPlaylist playlist{};

        check(
            HlsPlaylistParser::parse(
                content,
                playlist
            ),
            "Playlist with discontinuity parsed"
        );

        check(
            playlist.endList,
            "End list detected"
        );

        check(
            playlist.segments.size() == 3,
            "Discontinuity playlist segment count"
        );

        if (playlist.segments.size() != 3)
        {
            return;
        }

        check(
            !playlist.segments[0].discontinuity,
            "First segment has no discontinuity"
        );

        check(
            playlist.segments[1].discontinuity,
            "Discontinuity correctly assigned"
        );

        check(
            !playlist.segments[2].discontinuity,
            "Discontinuity flag correctly cleared"
        );
    }

    void testSlidingPlaylist()
    {
        const std::string content =
            "#EXTM3U\n"
            "#EXT-X-VERSION:6\n"
            "#EXT-X-TARGETDURATION:2\n"
            "#EXT-X-MEDIA-SEQUENCE:151\n"
            "#EXTINF:1.960000,\n"
            "segment_000151.ts\n"
            "#EXTINF:2.040000,\n"
            "segment_000152.ts\n"
            "#EXTINF:2.000000,\n"
            "segment_000153.ts\n";

        HlsPlaylist playlist{};

        check(
            HlsPlaylistParser::parse(
                content,
                playlist
            ),
            "Sliding playlist parsed"
        );

        check(
            playlist.mediaSequence == 151,
            "Sliding media sequence"
        );

        check(
            playlist.segments.size() == 3,
            "Sliding playlist segment count"
        );

        if (playlist.segments.size() != 3)
        {
            return;
        }

        check(
            playlist.segments.front()
                .sequenceNumber == 151,
            "Sliding playlist oldest sequence"
        );

        check(
            playlist.segments.back()
                .sequenceNumber == 153,
            "Sliding playlist newest sequence"
        );
    }

    void testOptionalFields()
    {
        const std::string content =
            "#EXTM3U\r\n"
            "#EXT-X-TARGETDURATION:2\r\n"
            "\r\n"
            "# Test comment\r\n"
            "#EXTINF:2.000000,\r\n"
            "segment_000000.ts\r\n";

        HlsPlaylist playlist{};

        check(
            HlsPlaylistParser::parse(
                content,
                playlist
            ),
            "CRLF and optional fields supported"
        );

        check(
            playlist.mediaSequence == 0,
            "Default media sequence is zero"
        );

        check(
            playlist.segments.size() == 1 &&
            playlist.segments.front()
                .sequenceNumber == 0,
            "Default segment sequence"
        );

        check(
            !playlist.independentSegments,
            "Optional independent segments flag"
        );
    }

    void testInvalidPlaylists()
    {
        expectInvalid(
            "",
            "Empty playlist rejected"
        );

        expectInvalid(
            "#EXT-X-TARGETDURATION:2\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Missing header rejected"
        );

        expectInvalid(
            "#EXTM3U\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Missing target duration rejected"
        );

        expectInvalid(
            validHeader,
            "Playlist without segments rejected"
        );

        expectInvalid(
            validHeader +
            "segment.ts\n",
            "Missing EXTINF rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:2.0,\n",
            "Incomplete segment rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:2.0,\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Duplicate EXTINF rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:0.0,\n"
            "segment.ts\n",
            "Zero duration rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:-2.0,\n"
            "segment.ts\n",
            "Negative duration rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:abc,\n"
            "segment.ts\n",
            "Invalid duration rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:nan,\n"
            "segment.ts\n",
            "NaN duration rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:inf,\n"
            "segment.ts\n",
            "Infinite duration rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:2.0abc,\n"
            "segment.ts\n",
            "Trailing duration characters rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:2.0\n"
            "segment.ts\n",
            "Missing EXTINF comma rejected"
        );

        expectInvalid(
            "#EXTM3U\n"
            "#EXT-X-TARGETDURATION:0\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Zero target duration rejected"
        );

        expectInvalid(
            "#EXTM3U\n"
            "#EXT-X-TARGETDURATION:2\n"
            "#EXT-X-MEDIA-SEQUENCE:-1\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Negative media sequence rejected"
        );

        expectInvalid(
            validHeader +
            "#EXT-X-MEDIA-SEQUENCE:151\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Duplicate media sequence rejected"
        );

        expectInvalid(
            validHeader +
            "#EXT-X-VERSION:7\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Duplicate version rejected"
        );

        expectInvalid(
            validHeader +
            "#EXT-X-TARGETDURATION:3\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Duplicate target duration rejected"
        );

        expectInvalid(
            validPlaylist +
            "#EXT-X-DISCONTINUITY\n",
            "Incomplete discontinuity rejected"
        );

        expectInvalid(
            validPlaylist +
            "#EXT-X-ENDLIST\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Segment after end list rejected"
        );
    }

    void testInvalidPaths()
    {
        const std::string prefix =
            validHeader +
            "#EXTINF:2.0,\n";

        expectInvalid(
            prefix + "../segment.ts\n",
            "Parent directory path rejected"
        );

        expectInvalid(
            prefix + "/tmp/segment.ts\n",
            "Absolute path rejected"
        );

        expectInvalid(
            prefix + "subdir/segment.ts\n",
            "Nested path rejected"
        );

        expectInvalid(
            prefix +
            "http://example.com/segment.ts\n",
            "Remote segment rejected"
        );

        expectInvalid(
            prefix + "segment.mp4\n",
            "Non MPEG-TS extension rejected"
        );

        expectInvalid(
            prefix + "segment.ts?token=123\n",
            "Query string rejected"
        );

        expectInvalid(
            prefix + "segment%2Ffile.ts\n",
            "Encoded path rejected"
        );
    }

    void testUnsupportedFormats()
    {
        expectInvalid(
            "#EXTM3U\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=2000000\n"
            "variant.m3u8\n",
            "Master playlist rejected"
        );

        expectInvalid(
            validHeader +
            "#EXT-X-KEY:METHOD=AES-128\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Encrypted playlist rejected"
        );

        expectInvalid(
            validHeader +
            "#EXT-X-MAP:URI=\"init.mp4\"\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Initialization map rejected"
        );

        expectInvalid(
            validHeader +
            "#EXTINF:2.0,\n"
            "#EXT-X-BYTERANGE:1000@0\n"
            "segment.ts\n",
            "Byte range rejected"
        );

        expectInvalid(
            validHeader +
            "#EXT-X-GAP\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Gap segment rejected"
        );
    }

    void testOverflow()
    {
        expectInvalid(
            "#EXTM3U\n"
            "#EXT-X-TARGETDURATION:2\n"
            "#EXT-X-MEDIA-SEQUENCE:"
            "18446744073709551616\n"
            "#EXTINF:2.0,\n"
            "segment.ts\n",
            "Media sequence overflow rejected"
        );

        expectInvalid(
            "#EXTM3U\n"
            "#EXT-X-TARGETDURATION:2\n"
            "#EXT-X-MEDIA-SEQUENCE:"
            "18446744073709551615\n"
            "#EXTINF:2.0,\n"
            "segment_1.ts\n"
            "#EXTINF:2.0,\n"
            "segment_2.ts\n",
            "Segment sequence overflow rejected"
        );
    }

    void testOutputPreservation()
    {
        HlsPlaylist playlist{};

        const bool initialized =
            HlsPlaylistParser::parse(
                validPlaylist,
                playlist
            );

        check(
            initialized,
            "Initialize output preservation test"
        );

        if (!initialized)
        {
            return;
        }

        check(
            !HlsPlaylistParser::parse(
                "invalid playlist",
                playlist
            ),
            "Invalid replacement rejected"
        );

        check(
            playlist.mediaSequence == 150 &&
            playlist.segments.size() == 3,
            "Previous valid output preserved"
        );
    }

    void testRealPlaylist(
        const std::string& filePath
    )
    {
        HlsPlaylist playlist{};

        const bool parsed =
            HlsPlaylistParser::parseFile(
                filePath,
                playlist
            );

        check(
            parsed,
            "Real FFmpeg playlist parsed"
        );

        if (!parsed)
        {
            return;
        }

        check(
            !playlist.segments.empty(),
            "Real playlist contains segments"
        );

        check(
            playlist.targetDurationSeconds > 0,
            "Real playlist target duration"
        );

        std::uint64_t totalDurationMs = 0;

        bool validSegments = true;

        for (std::size_t i = 0;
             i < playlist.segments.size();
             ++i)
        {
            const auto& segment =
                playlist.segments[i];

            if (segment.durationMs == 0 ||
                segment.sequenceNumber !=
                    playlist.mediaSequence + i)
            {
                validSegments = false;
            }

            totalDurationMs +=
                segment.durationMs;
        }

        check(
            validSegments,
            "Real playlist segment metadata valid"
        );

        std::cout
            << "\nReal playlist information:\n"
            << "File: "
            << filePath << '\n'
            << "Version: "
            << playlist.version << '\n'
            << "Media sequence: "
            << playlist.mediaSequence << '\n'
            << "Target duration: "
            << playlist.targetDurationSeconds
            << " s\n"
            << "Segments: "
            << playlist.segments.size() << '\n'
            << "Total duration: "
            << totalDurationMs << " ms\n"
            << "End list: "
            << (playlist.endList ? "YES" : "NO")
            << '\n';
    }
}

int main(int argc, char* argv[])
{
    std::cout
        << "IPTV M10 HLS Playlist Parser Test\n\n";

    testValidPlaylist();

    testDiscontinuity();

    testSlidingPlaylist();

    testOptionalFields();

    testInvalidPlaylists();

    testInvalidPaths();

    testUnsupportedFormats();

    testOverflow();

    testOutputPreservation();

    if (argc > 1)
    {
        testRealPlaylist(argv[1]);
    }

    std::cout
        << "\n=============================\n"
        << "Passed: " << passed << '\n'
        << "Failed: " << failed << '\n'
        << "Result: "
        << (failed == 0 ? "PASS" : "FAIL")
        << '\n'
        << "=============================\n";

    return failed == 0 ? 0 : 1;
}
