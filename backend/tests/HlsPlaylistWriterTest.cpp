#include "HlsPlaylistWriter.hpp"
#include "HlsPlaylistParser.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

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
            std::cout << "[PASS] "
                      << description << '\n';
        }
        else
        {
            ++failed;
            std::cerr << "[FAIL] "
                      << description << '\n';
        }
    }

    class TestDirectory
    {
    public:
        TestDirectory()
        {
            const std::string pattern =
                (fs::temp_directory_path() /
                 "iptv-writer-test-XXXXXX").string();

            std::vector<char> writable(
                pattern.begin(),
                pattern.end()
            );

            writable.push_back('\0');

            const char* directory =
                ::mkdtemp(writable.data());

            if (directory == nullptr)
            {
                throw std::runtime_error(
                    "Failed to create test directory"
                );
            }

            path_ = directory;
        }

        ~TestDirectory()
        {
            std::error_code error;
            fs::remove_all(path_, error);
        }

        TestDirectory(
            const TestDirectory&
        ) = delete;

        TestDirectory& operator=(
            const TestDirectory&
        ) = delete;

        const fs::path& path() const
        {
            return path_;
        }

    private:
        fs::path path_;
    };

    SegmentMetadata createSegment(
        const fs::path& directory,
        std::uint64_t sequence,
        std::uint64_t start,
        std::uint64_t duration,
        bool discontinuity = false
    )
    {
        SegmentMetadata segment{};

        segment.sequenceNumber = sequence;

        segment.filePath =
            directory /
            ("segment_" +
             std::to_string(sequence) +
             ".ts");

        segment.startTimeMs = start;
        segment.durationMs = duration;
        segment.sizeBytes = 188;
        segment.discontinuity = discontinuity;

        std::ofstream output(
            segment.filePath,
            std::ios::binary
        );

        const std::vector<char> data(
            188,
            0x47
        );

        output.write(
            data.data(),
            data.size()
        );

        output.close();

        if (!output)
        {
            throw std::runtime_error(
                "Failed to create segment"
            );
        }

        return segment;
    }

    std::string readFile(
        const fs::path& path
    )
    {
        std::ifstream input(
            path,
            std::ios::binary
        );

        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );
    }

    void testValidPlaylist()
    {
        TestDirectory directory;

        const fs::path outputPath =
            directory.path() / "index.m3u8";

        const std::vector<SegmentMetadata> segments =
        {
            createSegment(
                directory.path(), 10, 0, 2000
            ),
            createSegment(
                directory.path(), 11, 2000, 1960
            ),
            createSegment(
                directory.path(), 12, 3960, 2040
            )
        };

        check(
            HlsPlaylistWriter::write(
                outputPath,
                segments,
                2
            ),
            "Valid playlist written"
        );

        check(
            fs::exists(outputPath),
            "Public playlist exists"
        );

        check(
            !fs::exists(
                directory.path() / "index.m3u8.tmp"
            ),
            "Temporary playlist removed"
        );

        HlsPlaylist playlist{};

        const bool parsed =
            HlsPlaylistParser::parseFile(
                outputPath,
                playlist
            );

        check(
            parsed,
            "Generated playlist successfully parsed"
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
            playlist.mediaSequence == 10,
            "Correct media sequence"
        );

        check(
            playlist.independentSegments,
            "Independent segments flag"
        );

        check(
            !playlist.endList,
            "Live playlist has no end list"
        );

        check(
            playlist.segments.size() == 3,
            "Correct segment count"
        );

        if (playlist.segments.size() != 3)
        {
            return;
        }

        check(
            playlist.segments[0].durationMs == 2000 &&
            playlist.segments[1].durationMs == 1960 &&
            playlist.segments[2].durationMs == 2040,
            "Exact segment durations preserved"
        );

        check(
            playlist.segments[0].fileName ==
                "segment_10.ts" &&
            playlist.segments[1].fileName ==
                "segment_11.ts" &&
            playlist.segments[2].fileName ==
                "segment_12.ts",
            "Relative segment filenames"
        );

        check(
            readFile(outputPath).find(
                "#EXT-X-DISCONTINUITY-SEQUENCE:0"
            ) != std::string::npos,
            "Initial discontinuity sequence"
        );
    }

    void testDiscontinuityAndEndList()
    {
        TestDirectory directory;

        const fs::path outputPath =
            directory.path() / "index.m3u8";

        const std::vector<SegmentMetadata> segments =
        {
            createSegment(
                directory.path(), 20, 0, 2000
            ),
            createSegment(
                directory.path(), 21, 2000, 2000,
                true
            ),
            createSegment(
                directory.path(), 22, 4000, 2000
            )
        };

        check(
            HlsPlaylistWriter::write(
                outputPath,
                segments,
                2,
                false,
                true,
                3
            ),
            "Playlist with discontinuity written"
        );

        HlsPlaylist playlist{};

        const bool parsed =
            HlsPlaylistParser::parseFile(
                outputPath,
                playlist
            );

        check(
            parsed,
            "Discontinuity playlist parsed"
        );

        if (!parsed)
        {
            return;
        }

        check(
            playlist.endList,
            "End list preserved"
        );

        check(
            !playlist.independentSegments,
            "Independent segments disabled"
        );

        check(
            playlist.segments.size() == 3 &&
            !playlist.segments[0].discontinuity &&
            playlist.segments[1].discontinuity &&
            !playlist.segments[2].discontinuity,
            "Discontinuity correctly positioned"
        );

        check(
            readFile(outputPath).find(
                "#EXT-X-DISCONTINUITY-SEQUENCE:3"
            ) != std::string::npos,
            "Discontinuity sequence preserved"
        );
    }

    void testSlidingPlaylist()
    {
        TestDirectory directory;

        const fs::path outputPath =
            directory.path() / "index.m3u8";

        std::vector<SegmentMetadata> segments =
        {
            createSegment(
                directory.path(), 0, 0, 2000
            ),
            createSegment(
                directory.path(), 1, 2000, 2000
            ),
            createSegment(
                directory.path(), 2, 4000, 2000
            )
        };

        check(
            HlsPlaylistWriter::write(
                outputPath,
                segments,
                2
            ),
            "Initial sliding playlist written"
        );

        segments.erase(segments.begin());

        segments.push_back(
            createSegment(
                directory.path(), 3, 6000, 2000
            )
        );

        check(
            HlsPlaylistWriter::write(
                outputPath,
                segments,
                2
            ),
            "Existing playlist replaced"
        );

        HlsPlaylist playlist{};

        const bool parsed =
            HlsPlaylistParser::parseFile(
                outputPath,
                playlist
            );

        check(
            parsed,
            "Updated playlist parsed"
        );

        if (!parsed)
        {
            return;
        }

        check(
            playlist.mediaSequence == 1,
            "Sliding media sequence updated"
        );

        check(
            playlist.segments.size() == 3,
            "Sliding playlist size preserved"
        );

        check(
            playlist.segments.size() == 3 &&
            playlist.segments.front()
                .sequenceNumber == 1 &&
            playlist.segments.back()
                .sequenceNumber == 3,
            "Correct sliding segment interval"
        );

        check(
            readFile(outputPath).find(
                "segment_0.ts"
            ) == std::string::npos,
            "Expired segment absent from playlist"
        );

        check(
            fs::exists(
                directory.path() / "segment_0.ts"
            ),
            "Writer does not delete media files"
        );

        check(
            !fs::exists(
                directory.path() / "index.m3u8.tmp"
            ),
            "No temporary file after replacement"
        );
    }

    void testInvalidInput()
    {
        TestDirectory directory;

        const fs::path outputPath =
            directory.path() / "index.m3u8";

        const std::vector<SegmentMetadata> valid =
        {
            createSegment(
                directory.path(), 0, 0, 2000
            ),
            createSegment(
                directory.path(), 1, 2000, 2000
            )
        };

        check(
            !HlsPlaylistWriter::write(
                outputPath,
                {},
                2
            ),
            "Empty playlist rejected"
        );

        check(
            !HlsPlaylistWriter::write(
                outputPath,
                valid,
                0
            ),
            "Zero target duration rejected"
        );

        check(
            !HlsPlaylistWriter::write(
                {},
                valid,
                2
            ),
            "Empty output path rejected"
        );

        check(
            !HlsPlaylistWriter::write(
                directory.path() / "index.txt",
                valid,
                2
            ),
            "Invalid output extension rejected"
        );

        check(
            !HlsPlaylistWriter::write(
                directory.path() /
                    "missing" / "index.m3u8",
                valid,
                2
            ),
            "Missing output directory rejected"
        );

        auto invalid = valid;

        invalid[0].durationMs = 0;

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Zero segment duration rejected"
        );

        invalid = valid;

        invalid[0].durationMs = 3000;

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Excessive segment duration rejected"
        );

        invalid = valid;

        invalid[1].sequenceNumber = 3;

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Sequence gap rejected"
        );

        invalid = valid;

        invalid[1].startTimeMs = 3000;

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Timeline gap rejected"
        );

        invalid = valid;

        invalid[1].filePath =
            invalid[0].filePath;

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Duplicate filename rejected"
        );

        invalid = valid;

        invalid[1].filePath =
            directory.path() / "missing.ts";

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Missing segment rejected"
        );

        invalid = valid;

        invalid[0].startTimeMs =
            std::numeric_limits<
                std::uint64_t
            >::max();

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Timeline overflow rejected"
        );

        TestDirectory externalDirectory;

        invalid = valid;

        invalid[1] = createSegment(
            externalDirectory.path(),
            1,
            2000,
            2000
        );

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "External segment rejected"
        );

        const fs::path symlinkPath =
            directory.path() / "linked.ts";

        fs::create_symlink(
            valid[1].filePath,
            symlinkPath
        );

        invalid = valid;

        invalid[1].filePath = symlinkPath;

        check(
            !HlsPlaylistWriter::write(
                outputPath, invalid, 2
            ),
            "Symbolic link rejected"
        );
    }

    void testOutputPreservation()
    {
        TestDirectory directory;

        const fs::path outputPath =
            directory.path() / "index.m3u8";

        std::vector<SegmentMetadata> segments =
        {
            createSegment(
                directory.path(), 0, 0, 2000
            )
        };

        const bool written =
            HlsPlaylistWriter::write(
                outputPath,
                segments,
                2
            );

        check(
            written,
            "Initial valid output written"
        );

        if (!written)
        {
            return;
        }

        const std::string original =
            readFile(outputPath);

        segments[0].durationMs = 0;

        check(
            !HlsPlaylistWriter::write(
                outputPath,
                segments,
                2
            ),
            "Invalid replacement rejected"
        );

        check(
            readFile(outputPath) == original,
            "Previous valid playlist preserved"
        );

        check(
            !fs::exists(
                directory.path() / "index.m3u8.tmp"
            ),
            "No temporary file after failure"
        );
    }
}

int main()
{
    std::cout
        << "IPTV M10 HLS Playlist Writer Test\n\n";

    try
    {
        testValidPlaylist();
        testDiscontinuityAndEndList();
        testSlidingPlaylist();
        testInvalidInput();
        testOutputPreservation();
    }
    catch (const std::exception& exception)
    {
        ++failed;

        std::cerr
            << "[FAIL] Exception: "
            << exception.what() << '\n';
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
