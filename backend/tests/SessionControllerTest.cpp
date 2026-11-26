#include "SessionController.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

struct TestDirectory
{
    std::filesystem::path path =
        std::filesystem::path("runtime") /
        ("session_test_" + std::to_string(::getpid()));

    ~TestDirectory()
    {
        std::error_code error;

        std::filesystem::remove_all(path, error);
    }
};

void verifySession(
    const SessionController& controller,
    std::uint16_t expectedServiceId,
    std::uint64_t expectedGeneration
)
{
    const auto snapshot = controller.snapshot();

    require(
        snapshot.state == SessionState::Running,
        "Session is not running"
    );

    require(
        snapshot.activeService.has_value(),
        "Active service is missing"
    );

    require(
        snapshot.activeService->serviceId ==
            expectedServiceId,
        "Unexpected active service"
    );

    require(
        snapshot.generationId == expectedGeneration,
        "Unexpected session generation"
    );

    require(
        snapshot.playback.generationId ==
            expectedGeneration,
        "Playback generation mismatch"
    );

    require(
        snapshot.errorMessage.empty(),
        "Unexpected session error"
    );
}

void verifyReset(
    const SessionController& controller
)
{
    const auto snapshot = controller.snapshot();

    require(
        snapshot.playback.state ==
            PlaybackState::Buffering,
        "Playback was not reset to BUFFERING"
    );

    require(
        !snapshot.playback.bufferAvailable,
        "Previous buffer is still available"
    );

    require(
        snapshot.playback.bufferDurationMs == 0,
        "Buffer duration was not reset"
    );

    require(
        snapshot.playback.playbackPositionMs == 0,
        "Playback position was not reset"
    );

    require(
        snapshot.playback.seekRevision == 0,
        "Seek revision was not reset"
    );

    require(
        !snapshot.playback.seekRequired,
        "Previous seek request was retained"
    );
}

void waitForBuffer(
    SessionController& controller,
    const std::filesystem::path& directory
)
{
    const auto deadline =
        std::chrono::steady_clock::now() + 25s;

    while (
        std::chrono::steady_clock::now() < deadline
    )
    {
        require(
            controller.update(),
            "Session update failed: " +
                controller.snapshot().errorMessage
        );

        const auto snapshot =
            controller.snapshot();

        if (snapshot.playback.bufferAvailable &&
            snapshot.playback.bufferDurationMs >= 4000)
        {
            require(
                std::filesystem::is_regular_file(
                    directory / "index.m3u8"
                ),
                "Published HLS playlist is missing"
            );

            const auto statistics =
                controller.statistics();

            require(
                statistics.has_value(),
                "Session statistics are missing"
            );

            require(
                statistics->producer.validRtpPackets > 0,
                "No valid RTP packets received"
            );

            require(
                statistics->producer.validPatSections > 0,
                "No valid PAT sections received"
            );

            require(
                statistics->producer.validPmtSections > 0,
                "No valid PMT sections received"
            );

            require(
                statistics->consumer.registeredSegments > 0,
                "No HLS segments registered"
            );

            require(
                statistics->consumer.writeErrors == 0,
                "HLS write errors detected"
            );

            require(
                statistics->consumer.bufferUpdateErrors == 0,
                "Buffer update errors detected"
            );

            require(
                statistics->consumer.processErrors == 0,
                "HLS process errors detected"
            );

            return;
        }

        std::this_thread::sleep_for(100ms);
    }

    throw std::runtime_error(
        "Timeout waiting for HLS buffer"
    );
}

void verifySwitch(
    SessionController& controller,
    std::uint16_t serviceId,
    std::uint64_t generation,
    const std::filesystem::path& directory
)
{
    verifySession(
        controller,
        serviceId,
        generation
    );

    verifyReset(controller);

    waitForBuffer(controller, directory);

    verifySession(
        controller,
        serviceId,
        generation
    );
}

}

int main()
{
    try
    {
        TestDirectory directory;

        ServiceManager defaultServices;

        std::vector<TvService> catalog =
            defaultServices.services();

        for (auto& service : catalog)
        {
            service.rtpPort = 55004;
        }

        ServiceManager services(catalog);

        HlsConfig hlsConfig;

        hlsConfig.outputDirectory = directory.path;
        hlsConfig.segmentDurationSeconds = 2;
        hlsConfig.playlistSize = 60;
        hlsConfig.playlistFilename = "producer.m3u8";
        hlsConfig.deleteSegments = false;

        BufferConfig bufferConfig;

        bufferConfig.storageDirectory = directory.path;

        bufferConfig.maxDurationMs =
            30ULL * 60ULL * 1000ULL;

        bufferConfig.deletionMode =
            SegmentDeletionMode::Deferred;

        SessionController controller(
            services,
            hlsConfig,
            bufferConfig
        );

        require(
            controller.snapshot().state ==
                SessionState::Idle,
            "Invalid initial state"
        );

        require(
            !controller.nextChannel(),
            "Next channel accepted without session"
        );

        require(
            !controller.previousChannel(),
            "Previous channel accepted without session"
        );

        std::cout << "[PASS] Initial state\n";

        require(
            controller.start(),
            "Initial startup failed"
        );

        verifySwitch(
            controller, 1, 1, directory.path
        );

        std::cout << "[PASS] Initial service\n";

        require(
            !controller.start(),
            "Repeated start should be rejected"
        );

        verifySession(controller, 1, 1);

        std::cout << "[PASS] Repeated start\n";

        require(
            controller.nextChannel(),
            "Next channel failed"
        );

        verifySwitch(
            controller, 2, 2, directory.path
        );

        std::cout << "[PASS] Next channel 1 -> 2\n";

        require(
            controller.nextChannel(),
            "Next channel failed"
        );

        verifySwitch(
            controller, 3, 3, directory.path
        );

        std::cout << "[PASS] Next channel 2 -> 3\n";

        require(
            controller.nextChannel(),
            "Next channel wrap failed"
        );

        verifySwitch(
            controller, 1, 4, directory.path
        );

        std::cout << "[PASS] Next channel wrap 3 -> 1\n";

        require(
            controller.previousChannel(),
            "Previous channel wrap failed"
        );

        verifySwitch(
            controller, 3, 5, directory.path
        );

        std::cout << "[PASS] Previous channel wrap 1 -> 3\n";

        require(
            controller.selectChannel(2),
            "Direct channel selection failed"
        );

        verifySwitch(
            controller, 2, 6, directory.path
        );

        std::cout << "[PASS] Direct channel selection\n";

        const auto before = controller.snapshot();

        require(
            controller.selectChannel(2),
            "Repeated channel selection failed"
        );

        verifySession(controller, 2, 6);

        const auto after = controller.snapshot();

        require(
            after.playback.bufferAvailable,
            "Same channel selection reset buffer"
        );

        require(
            after.playback.bufferDurationMs >=
                before.playback.bufferDurationMs,
            "Same channel selection reduced buffer"
        );

        require(
            after.playback.seekRevision ==
                before.playback.seekRevision,
            "Same channel selection reset seek revision"
        );

        std::cout << "[PASS] Same channel without reset\n";

        require(
            !controller.selectChannel(99),
            "Invalid channel accepted"
        );

        require(
            !controller.selectService(99),
            "Invalid service accepted"
        );

        verifySession(controller, 2, 6);

        std::cout << "[PASS] Invalid channel and service\n";

        require(
            controller.selectService(1),
            "Direct service selection failed"
        );

        verifySwitch(
            controller, 1, 7, directory.path
        );

        std::cout << "[PASS] Direct service selection\n";

        controller.stop();

        auto stopped = controller.snapshot();

        require(
            stopped.state == SessionState::Idle,
            "Session did not return to IDLE"
        );

        require(
            stopped.playback.state ==
                PlaybackState::Stopped,
            "Playback did not stop"
        );

        require(
            !stopped.activeService.has_value(),
            "Active service was retained"
        );

        require(
            !controller.statistics().has_value(),
            "Stopped session retained statistics"
        );

        std::cout << "[PASS] Controlled shutdown\n";

        controller.stop();

        require(
            controller.snapshot().state ==
                SessionState::Idle,
            "Repeated shutdown failed"
        );

        std::cout << "[PASS] Repeated shutdown\n";

        require(
            controller.start(),
            "Session restart failed"
        );

        verifySwitch(
            controller, 1, 8, directory.path
        );

        std::cout << "[PASS] Restart with new generation\n";

        controller.stop();

        require(
            controller.snapshot().state ==
                SessionState::Idle,
            "Final shutdown failed"
        );

        std::cout
            << "\nAll SessionController tests passed\n";

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
