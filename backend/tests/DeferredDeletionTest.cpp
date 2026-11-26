#include "CircularDiskBuffer.hpp"
#include "HlsPlaylistWriter.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{
    int passed = 0;
    int failed = 0;

    void check(bool condition, const char* description)
    {
        if (condition)
        {
            ++passed;
            std::cout << "[PASS] ";
        }
        else
        {
            ++failed;
            std::cout << "[FAIL] ";
        }

        std::cout << description << '\n';
    }

    class TestDirectory
    {
    public:
        TestDirectory()
        {
            auto pattern =
                (fs::temp_directory_path() /
                 "iptv-deferred-XXXXXX").string();

            std::vector<char> data(
                pattern.begin(), pattern.end()
            );

            data.push_back('\0');

            const char* result =
                ::mkdtemp(data.data());

            if (!result)
            {
                throw std::runtime_error(
                    "Temporary directory creation failed"
                );
            }

            path = result;
        }

        ~TestDirectory()
        {
            std::error_code error;
            fs::remove_all(path, error);
        }

        fs::path path;
    };

    SegmentMetadata createSegment(
        const fs::path& directory,
        std::uint64_t sequence,
        bool discontinuity = false
    )
    {
        SegmentMetadata segment{};

        segment.sequenceNumber = sequence;

        segment.filePath =
            directory /
            ("segment_" +
             std::to_string(sequence) + ".ts");

        segment.startTimeMs = sequence * 2000;
        segment.durationMs = 2000;
        segment.discontinuity = discontinuity;

        std::ofstream output(
            segment.filePath,
            std::ios::binary
        );

        const std::vector<char> data(188, 0x47);

        output.write(data.data(), data.size());
        output.close();

        if (!output)
        {
            throw std::runtime_error(
                "Segment creation failed"
            );
        }

        return segment;
    }

    void testDeferredDeletion()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory = directory.path;
        config.maxDurationMs = 4000;
        config.deletionMode =
            SegmentDeletionMode::Deferred;

        config.deletionGrace = 100ms;

        CircularDiskBuffer buffer(config);

        if (!buffer.initialize())
        {
            throw std::runtime_error(
                "Buffer initialization failed"
            );
        }

        const auto first =
            createSegment(directory.path, 0, true);

        const auto second =
            createSegment(directory.path, 1);

        const auto third =
            createSegment(directory.path, 2);

        const auto playlistPath =
            directory.path / "index.m3u8";

        check(
            buffer.addSegment(first),
            "First segment registered"
        );

        check(
            buffer.addSegment(second),
            "Second segment registered"
        );

        check(
            HlsPlaylistWriter::write(
                playlistPath,
                buffer.segments(),
                2
            ),
            "Initial playlist published"
        );

        buffer.markPlaylistPublished();

        check(
            buffer.addSegment(third),
            "Third segment registered"
        );

        check(
            buffer.segmentCount() == 2,
            "Active buffer remains bounded"
        );

        check(
            buffer.pendingDeletionCount() == 1,
            "Expired segment queued for deletion"
        );

        check(
            fs::exists(first.filePath),
            "Expired file remains available"
        );

        check(
            buffer.availableInterval().startTimeMs
                == 2000,
            "Logical buffer start advanced"
        );

        check(
            buffer.discontinuitySequence() == 1,
            "Discontinuity sequence advanced"
        );

        std::this_thread::sleep_for(150ms);

        check(
            buffer.collectExpiredSegments(),
            "Collection before publication succeeds"
        );

        check(
            fs::exists(first.filePath),
            "No deletion before publication"
        );

        check(
            buffer.pendingDeletionCount() == 1,
            "Unpublished retirement remains queued"
        );

        const bool published =
            HlsPlaylistWriter::write(
                playlistPath,
                buffer.segments(),
                2,
                true,
                false,
                buffer.discontinuitySequence()
            );

        check(
            published,
            "Updated playlist published"
        );

        if (!published)
        {
            return;
        }

        buffer.markPlaylistPublished();

        check(
            fs::exists(first.filePath),
            "File retained after publication"
        );

        std::this_thread::sleep_for(150ms);

        check(
            buffer.collectExpiredSegments(),
            "Expired segment collection succeeds"
        );

        check(
            !fs::exists(first.filePath),
            "Expired file physically deleted"
        );

        check(
            buffer.pendingDeletionCount() == 0,
            "Deletion queue emptied"
        );

        check(
            fs::exists(second.filePath) &&
            fs::exists(third.filePath),
            "Active segments preserved"
        );

        const auto fourth =
            createSegment(directory.path, 3);

        check(
            buffer.addSegment(fourth),
            "Fourth segment registered"
        );

        check(
            buffer.pendingDeletionCount() == 1,
            "New retirement queued"
        );

        // Simulate a stopped playback session.

        fs::remove(playlistPath);

        check(
            buffer.reset(),
            "Deferred buffer reset succeeds"
        );

        check(
            buffer.empty(),
            "Active buffer empty after reset"
        );

        check(
            buffer.pendingDeletionCount() == 0,
            "Deletion queue empty after reset"
        );

        check(
            buffer.discontinuitySequence() == 0,
            "Discontinuity sequence reset"
        );

        check(
            !fs::exists(second.filePath) &&
            !fs::exists(third.filePath) &&
            !fs::exists(fourth.filePath),
            "All registered files removed"
        );
    }
}

int main()
{
    std::cout
        << "IPTV M10 Deferred Deletion Test\n\n";

    try
    {
        testDeferredDeletion();
    }
    catch (const std::exception& exception)
    {
        ++failed;

        std::cerr
            << "[FAIL] "
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
