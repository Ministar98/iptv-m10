#include "CircularDiskBuffer.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <cstdlib>

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

    class TestDirectory
    {
    public:
        TestDirectory()
        {
            std::string pattern =
                (fs::temp_directory_path() /
                 "iptv-buffer-test-XXXXXX").string();

            std::vector<char> name(
                pattern.begin(),
                pattern.end()
            );

            name.push_back('\0');

            char* result = ::mkdtemp(name.data());

            if (result == nullptr)
            {
                throw std::runtime_error(
                    "Cannot create test directory"
                );
            }

            path_ = result;
        }

        ~TestDirectory()
        {
            std::error_code error;

            fs::remove_all(path_, error);
        }

        TestDirectory(const TestDirectory&) = delete;

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
        std::size_t size
    )
    {
        SegmentMetadata segment{};

        segment.sequenceNumber = sequence;

        segment.startTimeMs = start;

        segment.durationMs = duration;

        segment.sizeBytes = size;

        segment.filePath =
            directory /
            (
                "segment_" +
                std::to_string(sequence) +
                ".ts"
            );

        std::ofstream output(
            segment.filePath,
            std::ios::binary |
            std::ios::trunc
        );

        const std::vector<char> data(
            size,
            static_cast<char>(0x47)
        );

        output.write(
            data.data(),
            static_cast<std::streamsize>(
                data.size()
            )
        );

        output.close();

        if (!output)
        {
            throw std::runtime_error(
                "Cannot create test segment"
            );
        }

        return segment;
    }

    void testConfiguration()
    {
        BufferConfig defaults{};

        check(
            defaults.maxDurationMs == 1800000,
            "Default duration is 30 minutes"
        );

        check(
            defaults.maxStorageBytes ==
                15ULL * 1024ULL * 1024ULL * 1024ULL,
            "Default storage limit is 15 GiB"
        );

        TestDirectory directory;

        BufferConfig invalid{};

        invalid.storageDirectory =
            directory.path();

        invalid.maxDurationMs = 0;

        CircularDiskBuffer invalidBuffer(
            invalid
        );

        check(
            !invalidBuffer.initialize(),
            "Zero duration configuration rejected"
        );

        invalid.maxDurationMs = 10000;

        invalid.maxStorageBytes = 0;

        CircularDiskBuffer invalidStorage(
            invalid
        );

        check(
            !invalidStorage.initialize(),
            "Zero storage configuration rejected"
        );

        invalid.maxStorageBytes = 10000;

        invalid.storageDirectory.clear();

        CircularDiskBuffer invalidPath(
            invalid
        );

        check(
            !invalidPath.initialize(),
            "Empty storage directory rejected"
        );
    }

    void testEmptyBuffer()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path() / "storage";

        CircularDiskBuffer buffer(config);

        check(
            !buffer.addSegment(SegmentMetadata{}),
            "Segment rejected before initialization"
        );

        check(
            !buffer.reset(),
            "Reset rejected before initialization"
        );

        check(
            buffer.initialize(),
            "Buffer initialization"
        );

        check(
            fs::is_directory(
                config.storageDirectory
            ),
            "Storage directory created"
        );

        check(
            buffer.initialize(),
            "Repeated initialization"
        );

        check(
            buffer.empty() &&
            buffer.segmentCount() == 0,
            "Initially empty buffer"
        );

        check(
            !buffer.availableInterval().available,
            "Empty buffer has no available interval"
        );

        check(
            !buffer.oldestSegment().has_value() &&
            !buffer.newestSegment().has_value(),
            "Empty buffer has no segments"
        );

        check(
            !buffer.findSegment(0).has_value(),
            "Seek in empty buffer"
        );

        check(
            !buffer.clampPosition(0).has_value(),
            "Clamp in empty buffer"
        );

        check(
            buffer.statistics().storageBytes == 0,
            "Empty buffer storage usage"
        );
    }

    void testSegmentManagement()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        config.maxDurationMs = 10000;

        config.maxStorageBytes = 10000;

        CircularDiskBuffer buffer(config);

        if (!buffer.initialize())
        {
            check(false, "Initialize segment test");
            return;
        }

        auto first = createSegment(
            directory.path(),
            0, 0, 2000, 100
        );

        // Deliberately incorrect metadata size.

        first.sizeBytes = 9999;

        check(
            buffer.addSegment(first),
            "Add first segment"
        );

        check(
            buffer.segmentCount() == 1,
            "First segment registered"
        );

        check(
            buffer.statistics().storageBytes == 100,
            "Actual file size used"
        );

        bool added = true;

        for (std::uint64_t i = 1; i < 5; ++i)
        {
            const auto segment = createSegment(
                directory.path(),
                i,
                i * 2000,
                2000,
                100
            );

            added =
                buffer.addSegment(segment) &&
                added;
        }

        check(
            added &&
            buffer.segmentCount() == 5,
            "Add multiple segments"
        );

        const auto interval =
            buffer.availableInterval();

        check(
            interval.available &&
            interval.startTimeMs == 0 &&
            interval.endTimeMs == 10000 &&
            interval.durationMs == 10000,
            "Correct available interval"
        );

        check(
            buffer.statistics().storageBytes == 500,
            "Correct total storage usage"
        );

        const auto oldest =
            buffer.oldestSegment();

        const auto newest =
            buffer.newestSegment();

        check(
            oldest &&
            oldest->sequenceNumber == 0,
            "Correct oldest segment"
        );

        check(
            newest &&
            newest->sequenceNumber == 4,
            "Correct newest segment"
        );

        const auto middle =
            buffer.findSegment(4500);

        check(
            middle &&
            middle->sequenceNumber == 2,
            "Find segment by media position"
        );

        const auto boundary =
            buffer.findSegment(4000);

        check(
            boundary &&
            boundary->sequenceNumber == 2,
            "Seek at segment boundary"
        );

        const auto before =
            buffer.findSegment(-5000);

        check(
            before &&
            before->sequenceNumber == 0,
            "Seek before buffer returns oldest"
        );

        const auto after =
            buffer.findSegment(20000);

        check(
            after &&
            after->sequenceNumber == 4,
            "Seek after buffer returns newest"
        );

        check(
            buffer.clampPosition(-5000) == 0,
            "Clamp negative position"
        );

        check(
            buffer.clampPosition(20000) == 9999,
            "Clamp position after buffer"
        );

        check(
            buffer.clampPosition(4500) == 4500,
            "Preserve valid position"
        );

        check(
            !buffer.addSegment(first),
            "Duplicate segment rejected"
        );

        auto duplicatePath = first;

        duplicatePath.sequenceNumber = 5;
        duplicatePath.startTimeMs = 10000;

        check(
            !buffer.addSegment(duplicatePath),
            "Duplicate file path rejected"
        );

        auto missing = first;

        missing.sequenceNumber = 5;
        missing.startTimeMs = 10000;

        missing.filePath =
            directory.path() / "missing.ts";

        check(
            !buffer.addSegment(missing),
            "Missing file rejected"
        );

        TestDirectory outsideDirectory;

        const auto outside = createSegment(
            outsideDirectory.path(),
            5, 10000, 2000, 100
        );

        check(
            !buffer.addSegment(outside),
            "File outside storage rejected"
        );

        check(
            fs::exists(outside.filePath),
            "Outside file preserved"
        );

        const auto validNext = createSegment(
            directory.path(),
            5, 10000, 2000, 100
        );

        auto invalid = validNext;

        invalid.durationMs = 0;

        check(
            !buffer.addSegment(invalid),
            "Zero duration rejected"
        );

        invalid = validNext;

        invalid.durationMs = 12000;

        check(
            !buffer.addSegment(invalid),
            "Oversized duration rejected"
        );

        invalid = validNext;

        invalid.sequenceNumber = 7;

        check(
            !buffer.addSegment(invalid),
            "Sequence gap rejected"
        );

        invalid = validNext;

        invalid.startTimeMs = 11000;

        check(
            !buffer.addSegment(invalid),
            "Timeline gap rejected"
        );

        invalid = validNext;

        invalid.startTimeMs = 9000;

        check(
            !buffer.addSegment(invalid),
            "Timeline overlap rejected"
        );

        invalid = validNext;

        invalid.startTimeMs =
            std::numeric_limits<std::uint64_t>::max();

        check(
            !buffer.addSegment(invalid),
            "Timestamp overflow rejected"
        );

        check(
            buffer.segmentCount() == 5 &&
            buffer.statistics().storageBytes == 500,
            "Invalid additions preserve buffer state"
        );

        check(
            buffer.addSegment(validNext),
            "Add segment beyond duration limit"
        );

        const auto shifted =
            buffer.availableInterval();

        check(
            shifted.startTimeMs == 2000 &&
            shifted.endTimeMs == 12000 &&
            shifted.durationMs == 10000,
            "Buffer window shifted correctly"
        );

        check(
            buffer.segmentCount() == 5,
            "Segment count remains bounded"
        );

        check(
            !fs::exists(first.filePath),
            "Oldest file physically removed"
        );

        check(
            buffer.statistics().totalAddedSegments == 6 &&
            buffer.statistics().totalRemovedSegments == 1,
            "Correct eviction statistics"
        );

        check(
            buffer.clampPosition(0) == 2000,
            "Expired position clamped to oldest"
        );

        const auto snapshot =
            buffer.segments();

        check(
            snapshot.size() == 5 &&
            snapshot.front().sequenceNumber == 1 &&
            snapshot.back().sequenceNumber == 5,
            "Correct segment index snapshot"
        );

        // Unregistered files must survive reset.

        const auto unregistered = createSegment(
            directory.path(),
            99, 0, 2000, 100
        );

        check(
            buffer.reset(),
            "Reset buffer"
        );

        check(
            buffer.empty() &&
            buffer.segmentCount() == 0,
            "Buffer empty after reset"
        );

        bool allRemoved = true;

        for (const auto& segment : snapshot)
        {
            allRemoved =
                !fs::exists(segment.filePath) &&
                allRemoved;
        }

        check(
            allRemoved,
            "Reset removes registered files"
        );

        check(
            fs::exists(unregistered.filePath),
            "Reset preserves unregistered files"
        );

        const auto resetStats =
            buffer.statistics();

        check(
            resetStats.storageBytes == 0 &&
            resetStats.durationMs == 0 &&
            resetStats.totalAddedSegments == 0 &&
            resetStats.totalRemovedSegments == 0,
            "Statistics cleared after reset"
        );

        const auto newSession = createSegment(
            directory.path(),
            0, 0, 2000, 100
        );

        check(
            buffer.addSegment(newSession),
            "Buffer reusable after reset"
        );
    }

    void testStorageLimit()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        config.maxDurationMs = 60000;

        config.maxStorageBytes = 250;

        CircularDiskBuffer buffer(config);

        if (!buffer.initialize())
        {
            check(false, "Initialize storage test");
            return;
        }

        const auto first = createSegment(
            directory.path(),
            0, 0, 2000, 100
        );

        const auto second = createSegment(
            directory.path(),
            1, 2000, 2000, 100
        );

        const auto third = createSegment(
            directory.path(),
            2, 4000, 2000, 100
        );

        check(
            buffer.addSegment(first) &&
            buffer.addSegment(second) &&
            buffer.addSegment(third),
            "Add segments with storage limit"
        );

        check(
            buffer.segmentCount() == 2 &&
            buffer.statistics().storageBytes == 200,
            "Storage limit enforced"
        );

        check(
            !fs::exists(first.filePath),
            "Storage eviction removes oldest file"
        );

        const auto oversized = createSegment(
            directory.path(),
            3, 6000, 2000, 300
        );

        check(
            !buffer.addSegment(oversized),
            "Segment larger than capacity rejected"
        );

        check(
            buffer.segmentCount() == 2,
            "Oversized segment preserves buffer"
        );
    }

    void testVariableDuration()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        config.maxDurationMs = 5000;

        config.maxStorageBytes = 10000;

        CircularDiskBuffer buffer(config);

        if (!buffer.initialize())
        {
            check(false, "Initialize duration test");
            return;
        }

        const auto first = createSegment(
            directory.path(),
            0, 0, 1900, 100
        );

        const auto second = createSegment(
            directory.path(),
            1, 1900, 2100, 100
        );

        const auto third = createSegment(
            directory.path(),
            2, 4000, 1800, 100
        );

        check(
            buffer.addSegment(first) &&
            buffer.addSegment(second) &&
            buffer.addSegment(third),
            "Variable duration segments accepted"
        );

        const auto interval =
            buffer.availableInterval();

        check(
            interval.startTimeMs == 1900 &&
            interval.endTimeMs == 5800 &&
            interval.durationMs == 3900,
            "Actual segment durations used"
        );

        check(
            buffer.segmentCount() == 2 &&
            !fs::exists(first.filePath),
            "Variable duration eviction"
        );
    }

    void testContinuousOperation()
    {
        TestDirectory directory;

        BufferConfig config{};

        config.storageDirectory =
            directory.path();

        config.maxDurationMs = 1800000;

        config.maxStorageBytes = 1000000;

        CircularDiskBuffer buffer(config);

        if (!buffer.initialize())
        {
            check(false, "Initialize continuous test");
            return;
        }

        constexpr std::uint64_t segmentTotal = 2000;

        bool success = true;

        for (std::uint64_t i = 0;
             i < segmentTotal;
             ++i)
        {
            const auto segment = createSegment(
                directory.path(),
                i,
                i * 2000,
                2000,
                188
            );

            if (!buffer.addSegment(segment))
            {
                success = false;
                break;
            }

            const auto stats =
                buffer.statistics();

            if (stats.durationMs >
                    config.maxDurationMs ||
                stats.storageBytes >
                    config.maxStorageBytes)
            {
                success = false;
                break;
            }
        }

        check(
            success,
            "Continuous operation with 2000 segments"
        );

        check(
            buffer.segmentCount() == 900,
            "30-minute buffer retains 900 segments"
        );

        const auto interval =
            buffer.availableInterval();

        check(
            interval.startTimeMs == 2200000 &&
            interval.endTimeMs == 4000000 &&
            interval.durationMs == 1800000,
            "Correct 30-minute rolling interval"
        );

        const auto stats =
            buffer.statistics();

        check(
            stats.totalAddedSegments == 2000 &&
            stats.totalRemovedSegments == 1100,
            "Continuous eviction statistics"
        );

        std::size_t filesOnDisk = 0;

        for (const auto& entry :
             fs::directory_iterator(directory.path()))
        {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".ts")
            {
                ++filesOnDisk;
            }
        }

        check(
            filesOnDisk == 900,
            "Physical file count remains bounded"
        );

        check(
            stats.storageBytes == 900ULL * 188ULL,
            "Continuous storage accounting"
        );

        check(
            buffer.reset() &&
            buffer.empty() &&
            fs::is_empty(directory.path()),
            "Continuous buffer reset"
        );
    }
}

int main()
{
    std::cout
        << "IPTV M10 Circular Disk Buffer Test\n\n";

    try
    {
        testConfiguration();

        testEmptyBuffer();

        testSegmentManagement();

        testStorageLimit();

        testVariableDuration();

        testContinuousOperation();
    }
    catch (const std::exception& error)
    {
        ++failed;

        std::cerr
            << "[FAIL] Unexpected exception: "
            << error.what() << '\n';
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
