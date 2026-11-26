#include "PlaybackManager.hpp"

#include <cstdlib>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    BufferInterval makeInterval(
        std::uint64_t start,
        std::uint64_t end
    )
    {
        return BufferInterval{
            true,
            start,
            end,
            end - start
        };
    }

    void testPausedPositionPreserved()
    {
        PlaybackManager manager;

        manager.reset(1);

        require(
            manager.updateBufferInterval(
                1, makeInterval(0, 60000)
            ),
            "Initial buffer interval"
        );

        require(manager.start(), "Start");
        require(manager.seek(20000), "Seek");

        auto state = manager.snapshot();

        require(
            manager.acknowledgeSeek(
                1, state.seekRevision, 20000
            ),
            "Acknowledge seek"
        );

        require(manager.pause(), "Pause");

        require(
            manager.updateBufferInterval(
                1, makeInterval(10000, 90000)
            ),
            "Extend buffer"
        );

        state = manager.snapshot();

        require(
            state.state == PlaybackState::Paused,
            "Playback remains paused"
        );

        require(
            state.playbackPositionMs == 20000,
            "Paused position preserved"
        );

        require(
            state.delayFromLiveMs == 70000,
            "Delay increases during pause"
        );

        require(
            !state.positionExpired,
            "Position has not expired"
        );

        require(
            !state.seekRequired,
            "No unnecessary seek"
        );

        std::cout
            << "[PASS] Paused position preserved\n";
    }

    void testExpiredPausedPosition()
    {
        PlaybackManager manager;

        manager.reset(2);

        require(
            manager.updateBufferInterval(
                2, makeInterval(0, 60000)
            ),
            "Initial buffer interval"
        );

        require(manager.start(), "Start");
        require(manager.seek(10000), "Seek");

        auto state = manager.snapshot();

        require(
            manager.acknowledgeSeek(
                2, state.seekRevision, 10000
            ),
            "Acknowledge seek"
        );

        require(manager.pause(), "Pause");

        const auto previousRevision =
            manager.snapshot().seekRevision;

        require(
            manager.updateBufferInterval(
                2, makeInterval(20000, 80000)
            ),
            "Move buffer window"
        );

        state = manager.snapshot();

        require(
            state.state == PlaybackState::Paused,
            "Expired position remains paused"
        );

        require(
            state.playbackPositionMs == 20000,
            "Clamp to oldest position"
        );

        require(
            state.positionExpired,
            "Expired position detected"
        );

        require(
            state.seekRequired,
            "Corrective seek requested"
        );

        require(
            state.seekRevision == previousRevision + 1,
            "New seek revision"
        );

        require(
            state.delayFromLiveMs == 60000,
            "Correct delay after expiration"
        );

        require(
            state.positionInBufferMs == 0,
            "Position at buffer start"
        );

        require(
            !manager.acknowledgeSeek(
                2, previousRevision, 10000
            ),
            "Reject obsolete seek acknowledgement"
        );

        require(
            manager.acknowledgeSeek(
                2, state.seekRevision, 20000
            ),
            "Acknowledge corrective seek"
        );

        state = manager.snapshot();

        require(
            !state.seekRequired &&
            !state.positionExpired,
            "Expiration resolved"
        );

        require(
            state.state == PlaybackState::Paused,
            "Acknowledgement preserves pause"
        );

        require(manager.resume(), "Resume");

        state = manager.snapshot();

        require(
            state.state == PlaybackState::Timeshift,
            "Resume in timeshift mode"
        );

        require(
            state.playbackPositionMs == 20000,
            "Resume from oldest position"
        );

        std::cout
            << "[PASS] Expired paused position\n";
    }

    void testExpiredPlayingPosition()
    {
        PlaybackManager manager;

        manager.reset(3);

        require(
            manager.updateBufferInterval(
                3, makeInterval(0, 60000)
            ),
            "Initial buffer interval"
        );

        require(manager.start(), "Start");
        require(manager.seek(5000), "Seek");

        auto state = manager.snapshot();

        require(
            manager.acknowledgeSeek(
                3, state.seekRevision, 5000
            ),
            "Acknowledge seek"
        );

        require(
            manager.updateBufferInterval(
                3, makeInterval(15000, 75000)
            ),
            "Move buffer window"
        );

        state = manager.snapshot();

        require(
            state.state == PlaybackState::Timeshift,
            "Playback remains active"
        );

        require(
            state.playbackPositionMs == 15000,
            "Playing position clamped"
        );

        require(
            state.positionExpired &&
            state.seekRequired,
            "Corrective seek required"
        );

        std::cout
            << "[PASS] Expired playing position\n";
    }

    void testRepeatedExpiration()
    {
        PlaybackManager manager;

        manager.reset(4);

        require(
            manager.updateBufferInterval(
                4, makeInterval(0, 60000)
            ),
            "Initial buffer interval"
        );

        require(manager.start(), "Start");
        require(manager.goOldest(), "Go oldest");
        require(manager.pause(), "Pause");

        require(
            manager.updateBufferInterval(
                4, makeInterval(2000, 62000)
            ),
            "First expiration"
        );

        const auto first = manager.snapshot();

        require(
            manager.updateBufferInterval(
                4, makeInterval(4000, 64000)
            ),
            "Second expiration"
        );

        const auto second = manager.snapshot();

        require(
            second.seekRevision ==
                first.seekRevision + 1,
            "Corrective seek superseded"
        );

        require(
            second.playbackPositionMs == 4000,
            "Latest oldest position"
        );

        require(
            !manager.acknowledgeSeek(
                4, first.seekRevision, 2000
            ),
            "Reject outdated corrective seek"
        );

        require(
            manager.acknowledgeSeek(
                4, second.seekRevision, 4000
            ),
            "Accept latest corrective seek"
        );

        require(
            manager.snapshot().state ==
                PlaybackState::Paused,
            "Pause preserved"
        );

        std::cout
            << "[PASS] Repeated position expiration\n";
    }

    void testExactOldestBoundary()
    {
        PlaybackManager manager;

        manager.reset(5);

        require(
            manager.updateBufferInterval(
                5, makeInterval(0, 60000)
            ),
            "Initial buffer interval"
        );

        require(manager.start(), "Start");
        require(manager.seek(10000), "Seek");

        auto state = manager.snapshot();

        require(
            manager.acknowledgeSeek(
                5, state.seekRevision, 10000
            ),
            "Acknowledge seek"
        );

        require(manager.pause(), "Pause");

        const auto revision =
            manager.snapshot().seekRevision;

        require(
            manager.updateBufferInterval(
                5, makeInterval(10000, 70000)
            ),
            "Move buffer to exact boundary"
        );

        state = manager.snapshot();

        require(
            !state.positionExpired,
            "Exact oldest boundary is valid"
        );

        require(
            !state.seekRequired,
            "No corrective seek"
        );

        require(
            state.seekRevision == revision,
            "Seek revision unchanged"
        );

        require(
            state.positionInBufferMs == 0,
            "Position at oldest boundary"
        );

        std::cout
            << "[PASS] Exact oldest boundary\n";
    }
}

int main()
{
    testPausedPositionPreserved();
    testExpiredPausedPosition();
    testExpiredPlayingPosition();
    testRepeatedExpiration();
    testExactOldestBoundary();

    std::cout
        << "\nAll playback expiration tests passed.\n";

    return EXIT_SUCCESS;
}
