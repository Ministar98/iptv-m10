#include "HlsBufferManager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
                 "iptv-manager-test-XXXXXX").string();

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

    std::string segmentName(
        std::uint64_t sequence
    )
    {
        return "segment_" +
               std::to_string(sequence) +
               ".ts";
    }

    void createSegment(
        const fs::path& directory,
        std::uint64_t sequence
    )
    {
        std::ofstream output(
            directory / segmentName(sequence),
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
    }

    void writePlaylist(
        const fs::path& directory,
        std::uint64_t firstSequence,
        const std::vector<std::uint64_t>& durations,
        bool endList = false
    )
    {
        std::ofstream output(
            directory / "producer.m3u8"
        );

        output
            << "#EXTM3U\n"
            << "#EXT-X-VERSION:6\n"
            << "#EXT-X-TARGETDURATION:3\n"
            << "#EXT-X-MEDIA-SEQUENCE:"
            << firstSequence << '\n'
            << "#EXT-X-INDEPENDENT-SEGMENTS\n";

        for (std::size_t i = 0;
             i < durations.size();
             ++i)
        {
            const auto duration = durations[i];

            output
                << "#EXTINF:"
                << duration / 1000
                << '.'
                << (duration % 1000) / 100
                << (duration % 100) / 10
                << duration % 10
                << ",\n"
                << segmentName(firstSequence + i)
                << '\n';
        }

        if (endList)
        {
            output << "#EXT-X-ENDLIST\n";
        }

        output.close();

        if (!output)
        {
            throw std::runtime_error(
                "Failed to write playlist"
            );
        }
    }

    void testInitialization()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        HlsBufferManager manager(config);

        check(
            manager.update().status ==
                HlsUpdateStatus::SegmentRejected,
            "Update rejected before initialization"
        );

        check(
            !manager.reset(),
            "Reset rejected before initialization"
        );

        check(
            manager.initialize(),
            "Manager initialized"
        );

        check(
            manager.initialize(),
            "Repeated initialization"
        );

        check(
            manager.buffer().empty(),
            "Initially empty buffer"
        );

        check(
            !manager.lastRegisteredSequence()
                .has_value(),
            "Initially no registered sequence"
        );

        check(
            manager.nextMediaPositionMs() == 0,
            "Initial media position is zero"
        );

        check(
            manager.update().status ==
                HlsUpdateStatus::PlaylistUnavailable,
            "Missing playlist detected"
        );

        std::ofstream invalid(
            directory.path() / "producer.m3u8"
        );

        invalid << "invalid playlist\n";
        invalid.close();

        check(
            manager.update().status ==
                HlsUpdateStatus::InvalidPlaylist,
            "Invalid playlist detected"
        );

        check(
            manager.buffer().empty(),
            "Invalid playlist preserves buffer"
        );
    }

    void testRegistration()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        config.maxDurationMs = 10000;

        HlsBufferManager manager(config);

        if (!manager.initialize())
        {
            throw std::runtime_error(
                "Manager initialization failed"
            );
        }

        for (std::uint64_t i = 0; i < 3; ++i)
        {
            createSegment(directory.path(), i);
        }

        writePlaylist(
            directory.path(),
            0,
            {2000, 1960, 2040}
        );

        auto result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::Updated,
            "Initial playlist registered"
        );

        check(
            result.addedSegments == 3,
            "Three new segments registered"
        );

        check(
            manager.buffer().segmentCount() == 3,
            "Correct initial buffer size"
        );

        check(
            manager.lastRegisteredSequence() == 2,
            "Correct last registered sequence"
        );

        check(
            manager.nextMediaPositionMs() == 6000,
            "Correct cumulative media position"
        );

        const auto segments =
            manager.buffer().segments();

        check(
            segments.size() == 3 &&
            segments[0].startTimeMs == 0 &&
            segments[1].startTimeMs == 2000 &&
            segments[2].startTimeMs == 3960,
            "Correct segment start positions"
        );

        check(
            manager.buffer().statistics()
                .storageBytes == 564,
            "Correct physical storage accounting"
        );

        result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::NoChanges,
            "Repeated playlist detected"
        );

        check(
            result.addedSegments == 0 &&
            manager.buffer().segmentCount() == 3,
            "Duplicate registration prevented"
        );

        createSegment(directory.path(), 3);
        createSegment(directory.path(), 4);

        writePlaylist(
            directory.path(),
            1,
            {1960, 2040, 2000, 2000}
        );

        result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::Updated,
            "Sliding playlist processed"
        );

        check(
            result.addedSegments == 2,
            "Only new segments registered"
        );

        check(
            manager.buffer().segmentCount() == 5,
            "Correct updated buffer size"
        );

        check(
            manager.nextMediaPositionMs() == 10000,
            "Continuous media timeline"
        );

        createSegment(directory.path(), 5);

        writePlaylist(
            directory.path(),
            2,
            {2040, 2000, 2000, 2000}
        );

        result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::Updated &&
            result.addedSegments == 1,
            "New segment beyond duration limit"
        );

        const auto interval =
            manager.buffer().availableInterval();

        check(
            interval.available &&
            interval.startTimeMs == 2000 &&
            interval.endTimeMs == 12000 &&
            interval.durationMs == 10000,
            "Rolling buffer interval correct"
        );

        check(
            manager.buffer().segmentCount() == 5,
            "Buffer size remains bounded"
        );

        check(
            !fs::exists(
                directory.path() / segmentName(0)
            ),
            "Expired segment removed"
        );

        check(
            manager.lastRegisteredSequence() == 5,
            "Sequence preserved after eviction"
        );

        check(
            manager.buffer().statistics()
                .totalRemovedSegments == 1,
            "Correct eviction statistics"
        );
    }

    void testMissingSegment()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        HlsBufferManager manager(config);

        if (!manager.initialize())
        {
            throw std::runtime_error(
                "Manager initialization failed"
            );
        }

        createSegment(directory.path(), 0);

        writePlaylist(
            directory.path(),
            0,
            {2000, 2000}
        );

        auto result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::SegmentRejected,
            "Missing segment rejected"
        );

        check(
            result.addedSegments == 1,
            "Successful partial registration preserved"
        );

        check(
            manager.lastRegisteredSequence() == 0,
            "Sequence not advanced on failure"
        );

        check(
            manager.nextMediaPositionMs() == 2000,
            "Timeline not advanced on failure"
        );

        createSegment(directory.path(), 1);

        result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::Updated &&
            result.addedSegments == 1,
            "Missing segment successfully retried"
        );

        check(
            manager.buffer().segmentCount() == 2,
            "Retry does not duplicate segments"
        );

        check(
            manager.nextMediaPositionMs() == 4000,
            "Timeline correct after retry"
        );
    }

    void testSequenceGap()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        HlsBufferManager manager(config);

        if (!manager.initialize())
        {
            throw std::runtime_error(
                "Manager initialization failed"
            );
        }

        createSegment(directory.path(), 0);

        writePlaylist(
            directory.path(),
            0,
            {2000}
        );

        check(
            manager.update().status ==
                HlsUpdateStatus::Updated,
            "Initial segment registered"
        );

        createSegment(directory.path(), 2);

        writePlaylist(
            directory.path(),
            2,
            {2000}
        );

        const auto result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::SequenceGap,
            "Sequence gap detected"
        );

        check(
            result.addedSegments == 0,
            "Segment after gap not registered"
        );

        check(
            manager.lastRegisteredSequence() == 0 &&
            manager.nextMediaPositionMs() == 2000,
            "Sequence gap preserves timeline"
        );

        check(
            manager.buffer().segmentCount() == 1,
            "Sequence gap preserves buffer"
        );
    }

    void testResetAndEndList()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        HlsBufferManager manager(config);

        if (!manager.initialize())
        {
            throw std::runtime_error(
                "Manager initialization failed"
            );
        }

        createSegment(directory.path(), 10);
        createSegment(directory.path(), 11);

        writePlaylist(
            directory.path(),
            10,
            {2000, 2000},
            true
        );

        auto result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::Updated,
            "Nonzero initial sequence accepted"
        );

        check(
            result.endList,
            "End list propagated"
        );

        check(
            manager.lastRegisteredSequence() == 11,
            "Nonzero sequence tracked correctly"
        );

        check(
            manager.nextMediaPositionMs() == 4000,
            "Session-relative timeline"
        );

        result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::NoChanges &&
            result.endList,
            "End list preserved without new segments"
        );

        check(
            manager.reset(),
            "Manager reset successful"
        );

        check(
            manager.buffer().empty(),
            "Buffer empty after reset"
        );

        check(
            !manager.lastRegisteredSequence()
                .has_value(),
            "Sequence cleared after reset"
        );

        check(
            manager.nextMediaPositionMs() == 0,
            "Timeline cleared after reset"
        );

        check(
            !fs::exists(
                directory.path() / segmentName(10)
            ) &&
            !fs::exists(
                directory.path() / segmentName(11)
            ),
            "Registered files removed after reset"
        );

        fs::remove(
            directory.path() / "producer.m3u8"
        );

        createSegment(directory.path(), 0);

        writePlaylist(
            directory.path(),
            0,
            {2000}
        );

        result = manager.update();

        check(
            result.status ==
                HlsUpdateStatus::Updated &&
            result.addedSegments == 1,
            "New session registered after reset"
        );

        check(
            manager.lastRegisteredSequence() == 0 &&
            manager.nextMediaPositionMs() == 2000,
            "New session timeline correct"
        );
    }
}

int main()
{
    std::cout
        << "IPTV M10 HLS Buffer Manager Test\n\n";

    try
    {
        testInitialization();
        testRegistration();
        testMissingSegment();
        testSequenceGap();
        testResetAndEndList();
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
