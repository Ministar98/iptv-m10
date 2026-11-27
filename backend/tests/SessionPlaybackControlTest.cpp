#include "SessionController.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <unistd.h>

namespace
{

using namespace std::chrono_literals;

void require(
    bool condition,
    const std::string& message
)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void pass(const std::string& message)
{
    std::cout << "[PASS] " << message << '\n';
}

class TestDirectory
{
public:
    const std::filesystem::path path =
        std::filesystem::path("runtime") /
        ("playback_control_test_" +
         std::to_string(::getpid()));

    ~TestDirectory()
    {
        std::error_code error;

        std::filesystem::remove_all(path, error);
    }
};

SessionSnapshot waitForBuffer(
    SessionController& controller
)
{
    const auto deadline =
        std::chrono::steady_clock::now() + 25s;

    while (std::chrono::steady_clock::now() < deadline)
    {
        require(
            controller.update(),
            "Session update failed"
        );

        const auto snapshot = controller.snapshot();

        if (snapshot.playback.bufferAvailable &&
            snapshot.playback.bufferDurationMs >= 6000)
        {
            return snapshot;
        }

        std::this_thread::sleep_for(100ms);
    }

    throw std::runtime_error(
        "Timeout waiting for HLS buffer"
    );
}

void testPlaybackControl()
{
    TestDirectory directory;

    HlsConfig hlsConfig;

    hlsConfig.outputDirectory = directory.path;
    hlsConfig.segmentDurationSeconds = 2;
    hlsConfig.playlistSize = 60;
    hlsConfig.playlistFilename = "producer.m3u8";
    hlsConfig.deleteSegments = false;

    BufferConfig bufferConfig;

    bufferConfig.storageDirectory = directory.path;
    bufferConfig.deletionMode =
        SegmentDeletionMode::Deferred;

    const ServiceManager services;

    SessionController controller(
        services,
        hlsConfig,
        bufferConfig
    );

    require(
        controller.services().size() == 3,
        "Invalid service catalog"
    );

    pass("Service catalog access");

    require(
        !controller.pause(0) &&
        !controller.resume(0) &&
        !controller.seek(0, 0) &&
        !controller.goLive(0) &&
        !controller.goOldest(0),
        "Idle session accepted playback commands"
    );

    pass("Idle session rejects playback commands");

    require(
        controller.start(),
        "Session startup failed"
    );

    auto snapshot = waitForBuffer(controller);

    const auto generation = snapshot.generationId;

    require(
        snapshot.state == SessionState::Running &&
        snapshot.playback.generationId == generation,
        "Invalid session generation"
    );

    pass("Active session and published buffer");

    require(
        controller.goLive(generation),
        "Go Live failed"
    );

    snapshot = controller.snapshot();

    require(
        snapshot.playback.seekRequired &&
        snapshot.playback.playbackPositionMs ==
            snapshot.playback.livePositionMs,
        "Invalid live seek request"
    );

    pass("Go Live command");

    require(
        controller.acknowledgeSeek(
            generation,
            snapshot.playback.seekRevision,
            snapshot.playback.livePositionMs
        ),
        "Live seek acknowledgement failed"
    );

    require(
        !controller.snapshot().playback.seekRequired,
        "Seek request was not cleared"
    );

    pass("Seek acknowledgement");

    require(
        controller.pause(generation),
        "Pause failed"
    );

    snapshot = controller.snapshot();

    require(
        snapshot.playback.state ==
            PlaybackState::Paused,
        "Invalid paused state"
    );

    pass("Pause command");

    const auto pausedPosition =
        snapshot.playback.playbackPositionMs;

    const auto previousLive =
        snapshot.playback.livePositionMs;

    const auto deadline =
        std::chrono::steady_clock::now() + 10s;

    while (std::chrono::steady_clock::now() < deadline)
    {
        require(
            controller.update(),
            "Update while paused failed"
        );

        snapshot = controller.snapshot();

        if (snapshot.playback.livePositionMs >
            previousLive)
        {
            break;
        }

        std::this_thread::sleep_for(100ms);
    }

    require(
        snapshot.playback.livePositionMs > previousLive &&
        snapshot.playback.playbackPositionMs ==
            pausedPosition &&
        snapshot.playback.state ==
            PlaybackState::Paused,
        "Buffer did not grow during pause"
    );

    pass("Buffer continues growing during pause");

    require(
        controller.resume(generation),
        "Resume failed"
    );

    require(
        controller.snapshot().playback.state !=
            PlaybackState::Paused,
        "Playback remained paused"
    );

    pass("Resume command");

    require(
        controller.goOldest(generation),
        "Go Oldest failed"
    );

    snapshot = controller.snapshot();

    require(
        snapshot.playback.seekRequired &&
        snapshot.playback.playbackPositionMs ==
            snapshot.playback.oldestPositionMs,
        "Invalid oldest position"
    );

    pass("Go Oldest command");

    const auto target =
        snapshot.playback.oldestPositionMs + 1000;

    const auto oldRevision =
        snapshot.playback.seekRevision;

    require(
        controller.seek(
            generation,
            static_cast<std::int64_t>(target)
        ),
        "Seek failed"
    );

    snapshot = controller.snapshot();

    require(
        snapshot.playback.playbackPositionMs == target &&
        snapshot.playback.seekRequired &&
        snapshot.playback.seekRevision > oldRevision,
        "Invalid seek position or revision"
    );

    pass("Absolute timeline seek");

    require(
        !controller.acknowledgeSeek(
            generation,
            oldRevision,
            target
        ),
        "Old seek revision was accepted"
    );

    pass("Stale seek revision rejected");

    require(
        controller.acknowledgeSeek(
            generation,
            snapshot.playback.seekRevision,
            target
        ),
        "Seek acknowledgement failed"
    );

    require(
        controller.reportPlaybackPosition(
            generation,
            snapshot.playback.seekRevision,
            target + 500
        ),
        "Playback position report failed"
    );

    require(
        controller.snapshot().playback.playbackPositionMs ==
            target + 500,
        "Reported position was not stored"
    );

    pass("Browser playback position feedback");

    require(
        controller.nextChannel(),
        "Channel switching failed"
    );

    snapshot = waitForBuffer(controller);

    const auto newGeneration = snapshot.generationId;

    require(
        newGeneration != generation &&
        snapshot.activeService.has_value() &&
        snapshot.activeService->serviceId == 2,
        "Channel generation was not replaced"
    );

    pass("Channel switch creates new generation");

    require(
        !controller.pause(generation) &&
        !controller.resume(generation) &&
        !controller.seek(generation, 0) &&
        !controller.goLive(generation) &&
        !controller.goOldest(generation) &&
        !controller.acknowledgeSeek(
            generation,
            snapshot.playback.seekRevision,
            0
        ) &&
        !controller.reportPlaybackPosition(
            generation,
            snapshot.playback.seekRevision,
            0
        ),
        "Old generation command was accepted"
    );

    pass("All stale generation commands rejected");

    require(
        controller.pause(newGeneration),
        "New generation command failed"
    );

    require(
        controller.resume(newGeneration),
        "New generation resume failed"
    );

    pass("New generation accepts playback commands");

    controller.stop();

    snapshot = controller.snapshot();

    require(
        snapshot.state == SessionState::Idle &&
        snapshot.playback.state ==
            PlaybackState::Stopped &&
        !snapshot.activeService.has_value(),
        "Session shutdown failed"
    );

    pass("Clean session shutdown");
}

} // namespace

int main()
{
    try
    {
        testPlaybackControl();

        std::cout
            << "\nAll Session Playback Control tests passed.\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "[FAIL] "
            << error.what()
            << '\n';

        return 1;
    }
}
