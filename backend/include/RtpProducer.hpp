#pragma once

#include "BoundedTsQueue.hpp"
#include "UdpSocket.hpp"
#include "PsiSectionAssembler.hpp"
#include "PatParser.hpp"
#include "PmtParser.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>

struct RtpProducerStatistics
{
    std::uint64_t receivedDatagrams = 0;

    std::uint64_t validRtpPackets = 0;
    std::uint64_t invalidRtpPackets = 0;

    std::uint64_t unexpectedSsrcPackets = 0;
    std::uint64_t rtpDiscontinuities = 0;

    std::uint64_t validTsPackets = 0;
    std::uint64_t invalidTsPackets = 0;
    std::uint64_t transportErrors = 0;

    std::uint64_t validPatSections = 0;
    std::uint64_t invalidPatSections = 0;

    std::uint64_t validPmtSections = 0;
    std::uint64_t invalidPmtSections = 0;
    std::uint64_t unexpectedPmtSections = 0;

    std::uint64_t queuedBlocks = 0;
    std::uint64_t queuedBytes = 0;

    std::uint64_t droppedBlocks = 0;
    std::uint64_t droppedBytes = 0;

    std::uint64_t receiveTimeouts = 0;
    std::uint64_t receiveErrors = 0;

    std::size_t detectedPrograms = 0;
    std::size_t resolvedPmtTables = 0;

    std::map<std::uint16_t, std::uint16_t> programs;
    std::map<std::uint16_t, PmtTable> programTables;

    bool signalAvailable = false;
};

class RtpProducer
{
public:
    explicit RtpProducer(
        BoundedTsQueue& queue,
        std::uint16_t port = 5004
    );

    RtpProducer(const RtpProducer&) = delete;
    RtpProducer& operator=(const RtpProducer&) = delete;

    bool initialize();

    void run();

    void requestStop() noexcept;

    bool hasFatalError() const noexcept;

    bool isFinished() const noexcept;

    RtpProducerStatistics statistics() const;

    std::string lastError() const;

private:
    static constexpr unsigned int
        RECEIVE_TIMEOUT_MS = 200;

    static constexpr int
        RECEIVE_BUFFER_SIZE = 1024 * 1024;

    static constexpr std::chrono::seconds
        SIGNAL_TIMEOUT{3};

    static constexpr std::chrono::milliseconds
        STATISTICS_INTERVAL{250};

    BoundedTsQueue& queue_;

    const std::uint16_t port_;

    UdpSocket socket_;

    bool initialized_ = false;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> fatalError_{false};
    std::atomic<bool> finished_{false};

    mutable std::mutex stateMutex_;

    RtpProducerStatistics localStatistics_{};
    RtpProducerStatistics publishedStatistics_{};

    std::string lastError_;

    std::array<std::uint8_t, 65536>
        receiveBuffer_{};

    PsiSectionAssembler patAssembler_;

    std::map<
        std::uint16_t,
        PsiSectionAssembler
    > pmtAssemblers_;

    std::map<
        std::uint16_t,
        std::uint16_t
    > programs_;

    std::map<
        std::uint16_t,
        PmtTable
    > programTables_;

    std::set<
        std::tuple<
            std::uint16_t,
            std::uint8_t,
            std::uint8_t,
            bool
        >
    > printedPatSections_;

    std::set<
        std::tuple<
            std::uint16_t,
            std::uint16_t,
            std::uint8_t,
            bool
        >
    > printedPmtSections_;

    bool hasPreviousSequence_ = false;
    std::uint16_t previousSequence_ = 0;

    bool hasActiveSsrc_ = false;
    std::uint32_t activeSsrc_ = 0;

    bool signalLossReported_ = false;
    bool pendingDiscontinuity_ = false;

    std::chrono::steady_clock::time_point
        lastValidSignal_{};

    void resetAssemblers();

    void checkSignal();

    bool processDatagram(
        const std::uint8_t* data,
        std::size_t size
    );

    void processPat(const TsPacket& packet);

    void processPmt(const TsPacket& packet);

    void publishStatistics();

    void fail(const std::string& message);

    static void printPat(const PatTable& table);

    static void printPmt(
        std::uint16_t pmtPid,
        const PmtTable& table
    );
};
