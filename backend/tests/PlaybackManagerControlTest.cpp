#include "PlaybackManager.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

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
        return {true, start, end, end - start};
    }

    void testInitialState()
    {
        PlaybackManager manager;

        const auto state = manager.snapshot();

        require(
            state.state == PlaybackState::Stopped,
            "Initial stopped state"
        );

        require(!state.bufferAvailable, "Empty buffer");
        require(!state.seekRequired, "No pending seek");
        require(state.bufferDurationMs == 0, "Zero duration");
        require(state.delayFromLiveMs == 0, "Zero delay");
        require(state.errorMessage.empty(), "No initial error");

        std::cout << "[PASS] Initial state\n";
    }

    void testStartWithoutBuffer()
    {
        PlaybackManager manager;
        manager.reset(1);

        require(manager.start(), "Start without buffer");

        auto state = manager.snapshot();

        require(
            state.state == PlaybackState::Buffering,
            "Waiting for content"
        );

        require(!manager.start(), "Reject repeated start");
        require(!manager.pause(), "Reject pause while buffering");
        require(!manager.seek(1000), "Reject seek without buffer");

        require(
            manager.updateBufferInterval(
                1, makeInterval(0, 10000)
            ),
            "Publish first interval"
        );

        state = manager.snapshot();

        require(state.state == PlaybackState::Live, "Live state");
        require(state.seekRequired, "Initial seek requested");
        require(state.playbackPositionMs == 10000, "Live target");

        std::cout << "[PASS] Start without buffer\n";
    }

    void testStartWithBuffer()
    {
        PlaybackManager manager;
        manager.reset(2);

        require(
            manager.updateBufferInterval(
                2, makeInterval(10000, 70000)
            ),
            "Available interval"
        );

        require(manager.start(), "Start");

        const auto state = manager.snapshot();

        require(state.state == PlaybackState::Live, "Live state");
        require(state.atLiveEdge, "At live edge");
        require(state.playbackPositionMs == 70000, "Live position");
        require(state.bufferDurationMs == 60000, "Buffer duration");
        require(state.delayFromLiveMs == 0, "Live delay");
        require(state.positionInBufferMs == 60000, "Relative position");

        std::cout << "[PASS] Start with buffer\n";
    }

    void testPauseResume()
    {
        PlaybackManager manager;
        manager.reset(3);

        require(
            manager.updateBufferInterval(
                3, makeInterval(0, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");
        require(manager.seek(30000), "Seek");

        auto state = manager.snapshot();

        require(
            manager.acknowledgeSeek(
                3, state.seekRevision, 30000
            ),
            "Acknowledge seek"
        );

        require(manager.pause(), "Pause");
        require(!manager.pause(), "Reject repeated pause");

        require(
            !manager.reportPlaybackPosition(
                3, state.seekRevision, 31000
            ),
            "Reject position report while paused"
        );

        require(
            manager.updateBufferInterval(
                3, makeInterval(0, 80000)
            ),
            "Extend buffer during pause"
        );

        state = manager.snapshot();

        require(
            state.state == PlaybackState::Paused,
            "Paused state"
        );

        require(state.playbackPositionMs == 30000, "Fixed position");
        require(state.delayFromLiveMs == 50000, "Increased delay");

        require(manager.resume(), "Resume");
        require(!manager.resume(), "Reject repeated resume");

        state = manager.snapshot();

        require(
            state.state == PlaybackState::Timeshift,
            "Timeshift after resume"
        );

        require(state.playbackPositionMs == 30000, "Resume position");
        require(!state.seekRequired, "No unnecessary seek");

        std::cout << "[PASS] Pause and resume\n";
    }

    void testSeekBoundaries()
    {
        PlaybackManager manager;
        manager.reset(4);

        require(
            manager.updateBufferInterval(
                4, makeInterval(10000, 70000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");

        require(manager.seek(-5000), "Negative seek");

        auto state = manager.snapshot();

        require(
            state.playbackPositionMs == 10000,
            "Clamp negative seek"
        );

        require(manager.seek(5000), "Seek before oldest");

        require(
            manager.snapshot().playbackPositionMs == 10000,
            "Clamp to oldest"
        );

        require(manager.seek(90000), "Seek beyond live");

        require(
            manager.snapshot().playbackPositionMs == 70000,
            "Clamp to live"
        );

        require(manager.seek(40000), "Seek inside interval");

        state = manager.snapshot();

        require(state.playbackPositionMs == 40000, "Seek position");
        require(state.delayFromLiveMs == 30000, "Seek delay");
        require(state.positionInBufferMs == 30000, "Relative position");

        require(manager.pause(), "Pause before seek");
        require(manager.seek(20000), "Seek while paused");

        require(
            manager.snapshot().state == PlaybackState::Timeshift,
            "Seek resumes playback"
        );

        std::cout << "[PASS] Seek boundaries\n";
    }

    void testGoLiveAndOldest()
    {
        PlaybackManager manager;
        manager.reset(5);

        require(
            manager.updateBufferInterval(
                5, makeInterval(20000, 100000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");
        require(manager.goOldest(), "Go oldest");

        auto state = manager.snapshot();

        require(state.playbackPositionMs == 20000, "Oldest position");
        require(state.delayFromLiveMs == 80000, "Maximum delay");
        require(state.positionInBufferMs == 0, "Buffer start");

        require(
            state.state == PlaybackState::Timeshift,
            "Oldest timeshift state"
        );

        require(manager.goLive(), "Go live");

        state = manager.snapshot();

        require(state.state == PlaybackState::Live, "Live state");
        require(state.playbackPositionMs == 100000, "Live position");
        require(state.delayFromLiveMs == 0, "Zero live delay");
        require(state.atLiveEdge, "Live edge");

        std::cout << "[PASS] Go live and oldest\n";
    }

    void testSeekAcknowledgement()
    {
        PlaybackManager manager;
        manager.reset(6);

        require(
            manager.updateBufferInterval(
                6, makeInterval(10000, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");
        require(manager.seek(20000), "First seek");

        const auto first = manager.snapshot();

        require(manager.seek(40000), "Second seek");

        const auto second = manager.snapshot();

        require(
            second.seekRevision == first.seekRevision + 1,
            "Seek revision increment"
        );

        require(
            !manager.acknowledgeSeek(
                6, first.seekRevision, 20000
            ),
            "Reject stale acknowledgement"
        );

        require(
            !manager.acknowledgeSeek(
                7, second.seekRevision, 40000
            ),
            "Reject wrong generation"
        );

        require(
            !manager.acknowledgeSeek(
                6, second.seekRevision, 5000
            ),
            "Reject position below interval"
        );

        require(
            !manager.acknowledgeSeek(
                6, second.seekRevision, 70000
            ),
            "Reject position above interval"
        );

        require(
            !manager.reportPlaybackPosition(
                6, second.seekRevision, 40000
            ),
            "Reject report before acknowledgement"
        );

        require(
            manager.acknowledgeSeek(
                6, second.seekRevision, 41000
            ),
            "Accept actual seek position"
        );

        const auto state = manager.snapshot();

        require(!state.seekRequired, "Seek completed");
        require(state.playbackPositionMs == 41000, "Actual position");

        require(
            !manager.acknowledgeSeek(
                6, second.seekRevision, 41000
            ),
            "Reject duplicate acknowledgement"
        );

        std::cout << "[PASS] Seek acknowledgement\n";
    }

    void testPlaybackPositionReports()
    {
        PlaybackManager manager;
        manager.reset(7);

        require(
            manager.updateBufferInterval(
                7, makeInterval(10000, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");
        require(manager.seek(30000), "Seek");

        auto state = manager.snapshot();

        const auto revision = state.seekRevision;

        require(
            manager.acknowledgeSeek(7, revision, 30000),
            "Acknowledge seek"
        );

        require(
            manager.reportPlaybackPosition(7, revision, 35000),
            "Valid position report"
        );

        state = manager.snapshot();

        require(state.playbackPositionMs == 35000, "Updated position");
        require(state.delayFromLiveMs == 25000, "Updated delay");

        require(
            !manager.reportPlaybackPosition(8, revision, 36000),
            "Reject wrong generation"
        );

        require(
            !manager.reportPlaybackPosition(7, revision - 1, 36000),
            "Reject obsolete revision"
        );

        require(
            !manager.reportPlaybackPosition(7, revision, 9000),
            "Reject expired position"
        );

        require(
            !manager.reportPlaybackPosition(7, revision, 61000),
            "Reject position beyond live"
        );

        require(
            manager.snapshot().playbackPositionMs == 35000,
            "Rejected reports preserve position"
        );

        std::cout << "[PASS] Playback position reports\n";
    }

    void testLiveTolerance()
    {
        PlaybackManager manager(2000);
        manager.reset(8);

        require(
            manager.updateBufferInterval(
                8, makeInterval(0, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");

        const auto revision =
            manager.snapshot().seekRevision;

        require(
            manager.acknowledgeSeek(8, revision, 58000),
            "Acknowledge live position"
        );

        auto state = manager.snapshot();

        require(state.atLiveEdge, "Exact tolerance boundary");
        require(state.state == PlaybackState::Live, "Live state");

        require(
            manager.reportPlaybackPosition(8, revision, 57999),
            "Position outside tolerance"
        );

        state = manager.snapshot();

        require(!state.atLiveEdge, "Outside live tolerance");

        require(
            state.state == PlaybackState::Timeshift,
            "Timeshift state"
        );

        require(
            manager.reportPlaybackPosition(8, revision, 59000),
            "Return near live"
        );

        require(
            manager.snapshot().state == PlaybackState::Live,
            "Return to live state"
        );

        std::cout << "[PASS] Live tolerance\n";
    }

    void testInvalidIntervals()
    {
        PlaybackManager manager;
        manager.reset(9);

        require(
            !manager.updateBufferInterval(
                9, {true, 20000, 10000, 10000}
            ),
            "Reject reversed interval"
        );

        require(
            !manager.updateBufferInterval(
                9, {true, 10000, 10000, 0}
            ),
            "Reject zero duration"
        );

        require(
            !manager.updateBufferInterval(
                9, {true, 10000, 20000, 5000}
            ),
            "Reject incorrect duration"
        );

        require(
            !manager.updateBufferInterval(
                10, makeInterval(0, 60000)
            ),
            "Reject wrong generation"
        );

        require(
            manager.updateBufferInterval(
                9, makeInterval(10000, 60000)
            ),
            "Valid interval"
        );

        require(
            !manager.updateBufferInterval(
                9, makeInterval(5000, 60000)
            ),
            "Reject backwards oldest position"
        );

        require(
            !manager.updateBufferInterval(
                9, makeInterval(10000, 50000)
            ),
            "Reject backwards live position"
        );

        const auto state = manager.snapshot();

        require(state.oldestPositionMs == 10000, "Preserved oldest");
        require(state.livePositionMs == 60000, "Preserved live");

        std::cout << "[PASS] Invalid buffer intervals\n";
    }

    void testBufferLossAndRecovery()
    {
        PlaybackManager manager;
        manager.reset(10);

        require(
            manager.updateBufferInterval(
                10, makeInterval(0, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");

        require(
            manager.updateBufferInterval(10, {}),
            "Buffer unavailable"
        );

        auto state = manager.snapshot();

        require(!state.bufferAvailable, "Buffer unavailable flag");

        require(
            state.state == PlaybackState::Buffering,
            "Buffering after content loss"
        );

        require(!state.seekRequired, "Pending seek cleared");

        require(
            manager.updateBufferInterval(
                10, makeInterval(60000, 80000)
            ),
            "Restore buffer"
        );

        state = manager.snapshot();

        require(state.state == PlaybackState::Live, "Recovered live");
        require(state.seekRequired, "Recovery seek");
        require(state.playbackPositionMs == 80000, "Recovered position");

        std::cout << "[PASS] Buffer loss and recovery\n";
    }

    void testPausedBufferLoss()
    {
        PlaybackManager manager;
        manager.reset(11);

        require(
            manager.updateBufferInterval(
                11, makeInterval(0, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");
        require(manager.pause(), "Pause");

        require(
            manager.updateBufferInterval(11, {}),
            "Buffer loss while paused"
        );

        require(
            manager.snapshot().state == PlaybackState::Paused,
            "Preserve pause without buffer"
        );

        require(
            manager.updateBufferInterval(
                11, makeInterval(60000, 80000)
            ),
            "Restore buffer while paused"
        );

        const auto state = manager.snapshot();

        require(state.state == PlaybackState::Paused, "Still paused");
        require(state.positionExpired, "Old position expired");
        require(state.seekRequired, "Corrective seek");
        require(state.playbackPositionMs == 60000, "New oldest");

        std::cout << "[PASS] Paused buffer loss\n";
    }

    void testStopAndRestart()
    {
        PlaybackManager manager;
        manager.reset(12);

        require(
            manager.updateBufferInterval(
                12, makeInterval(10000, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");

        const auto oldRevision =
            manager.snapshot().seekRevision;

        manager.stop();

        auto state = manager.snapshot();

        require(state.state == PlaybackState::Stopped, "Stopped");
        require(state.bufferAvailable, "Buffer retained");
        require(!state.seekRequired, "Seek cancelled");

        require(!manager.pause(), "Reject pause while stopped");
        require(!manager.resume(), "Reject resume while stopped");
        require(!manager.goLive(), "Reject go live while stopped");

        require(manager.start(), "Restart");

        state = manager.snapshot();

        require(
            state.seekRevision > oldRevision,
            "Restart creates new revision"
        );

        require(
            !manager.acknowledgeSeek(
                12, oldRevision, 60000
            ),
            "Reject pre-stop acknowledgement"
        );

        std::cout << "[PASS] Stop and restart\n";
    }

    void testErrorAndReset()
    {
        PlaybackManager manager;
        manager.reset(13);

        require(
            manager.updateBufferInterval(
                13, makeInterval(0, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");

        manager.setError("Playback failure");

        auto state = manager.snapshot();

        require(state.state == PlaybackState::Error, "Error state");
        require(state.errorMessage == "Playback failure", "Error message");
        require(!state.seekRequired, "Pending seek cancelled");

        require(!manager.start(), "Reject start in error state");
        require(!manager.seek(20000), "Reject seek in error state");

        manager.reset(14);

        state = manager.snapshot();

        require(state.state == PlaybackState::Stopped, "Reset state");
        require(state.generationId == 14, "New generation");
        require(!state.bufferAvailable, "Buffer cleared");
        require(state.errorMessage.empty(), "Error cleared");
        require(state.seekRevision == 0, "Revision reset");

        require(
            !manager.updateBufferInterval(
                13, makeInterval(0, 60000)
            ),
            "Reject previous generation interval"
        );

        std::cout << "[PASS] Error and reset\n";
    }

    void testConcurrentAccess()
    {
        PlaybackManager manager;
        manager.reset(15);

        require(
            manager.updateBufferInterval(
                15, makeInterval(0, 60000)
            ),
            "Initial interval"
        );

        require(manager.start(), "Start");

        std::atomic<unsigned> ready{0};
        std::atomic<bool> start{false};

        auto waitForStart = [&]()
        {
            ++ready;

            while (!start.load())
            {
                std::this_thread::yield();
            }
        };

        std::thread bufferThread([&]()
        {
            waitForStart();

            for (std::uint64_t i = 1; i <= 10000; ++i)
            {
                require(
                    manager.updateBufferInterval(
                        15, makeInterval(i, i + 60000)
                    ),
                    "Concurrent buffer update"
                );
            }
        });

        std::thread controlThread([&]()
        {
            waitForStart();

            for (unsigned i = 0; i < 10000; ++i)
            {
                require(manager.goOldest(), "Concurrent oldest");
                require(manager.pause(), "Concurrent pause");
                require(manager.resume(), "Concurrent resume");
                require(manager.goLive(), "Concurrent live");
            }
        });

        std::thread readerThread([&]()
        {
            waitForStart();

            for (unsigned i = 0; i < 10000; ++i)
            {
                const auto state = manager.snapshot();

                require(state.bufferAvailable, "Available buffer");

                require(
                    state.oldestPositionMs <= state.playbackPositionMs,
                    "Position above oldest"
                );

                require(
                    state.playbackPositionMs <= state.livePositionMs,
                    "Position below live"
                );

                require(
                    state.bufferDurationMs ==
                        state.livePositionMs -
                        state.oldestPositionMs,
                    "Consistent duration"
                );

                require(
                    state.delayFromLiveMs ==
                        state.livePositionMs -
                        state.playbackPositionMs,
                    "Consistent delay"
                );

                require(
                    state.positionInBufferMs ==
                        state.playbackPositionMs -
                        state.oldestPositionMs,
                    "Consistent relative position"
                );
            }
        });

        while (ready.load() != 3)
        {
            std::this_thread::yield();
        }

        start.store(true);

        bufferThread.join();
        controlThread.join();
        readerThread.join();

        const auto state = manager.snapshot();

        require(state.oldestPositionMs == 10000, "Final oldest");
        require(state.livePositionMs == 70000, "Final live");
        require(state.bufferDurationMs == 60000, "Final duration");

        std::cout << "[PASS] Concurrent access\n";
    }
}

int main()
{
    testInitialState();
    testStartWithoutBuffer();
    testStartWithBuffer();
    testPauseResume();
    testSeekBoundaries();
    testGoLiveAndOldest();
    testSeekAcknowledgement();
    testPlaybackPositionReports();
    testLiveTolerance();
    testInvalidIntervals();
    testBufferLossAndRecovery();
    testPausedBufferLoss();
    testStopAndRestart();
    testErrorAndReset();
    testConcurrentAccess();

    std::cout
        << "\nAll playback control tests passed.\n";

    return EXIT_SUCCESS;
}
