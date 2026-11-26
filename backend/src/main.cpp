#include "UdpSocket.hpp"
#include "RtpParser.hpp"
#include "TsParser.hpp"
#include "PsiSectionAssembler.hpp"
#include "PatParser.hpp"
#include "PmtParser.hpp"
#include "HlsSegmenter.hpp"
#include "HlsBufferManager.hpp"
#include "HlsPlaylistWriter.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <system_error>

namespace
{
    constexpr std::uint16_t RTP_PORT = 5004;

    constexpr unsigned int RECEIVE_TIMEOUT_MS = 200;

    constexpr int RECEIVE_BUFFER_SIZE =
        1024 * 1024;

    constexpr int STATISTICS_INTERVAL_SECONDS = 5;

    constexpr int SIGNAL_TIMEOUT_SECONDS = 3;

    volatile std::sig_atomic_t stopRequested = 0;

    void handleSignal(int)
    {
        stopRequested = 1;
    }

    struct ReceiverStatistics
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

        std::uint64_t forwardedBytes = 0;

        std::uint64_t hlsWriteErrors = 0;

        std::uint64_t receiveTimeouts = 0;
    };

    void printPid(std::uint16_t pid)
    {
        std::cout
            << "0x"
            << std::hex
            << std::setw(4)
            << std::setfill('0')
            << pid
            << std::dec
            << std::setfill(' ')
            << " (" << pid << ")";
    }

    void printPat(const PatTable& table)
    {
        std::cout
            << "\n========== PAT ==========\n";

        std::cout
            << "Transport stream ID: "
            << table.transportStreamId << '\n';

        std::cout
            << "Version: "
            << static_cast<int>(
                table.versionNumber
            ) << '\n';

        std::cout
            << "Current table: "
            << (table.currentNextIndicator
                    ? "YES"
                    : "NO")
            << '\n';

        for (const auto& program :
             table.programs)
        {
            std::cout
                << "Program "
                << program.programNumber
                << " -> PMT PID ";

            printPid(program.pmtPid);

            std::cout << '\n';
        }

        if (table.hasNetworkPid)
        {
            std::cout << "Network PID: ";

            printPid(table.networkPid);

            std::cout << '\n';
        }

        std::cout << "CRC: VALID\n";

        std::cout
            << "=========================\n";
    }

    void printPmt(
        std::uint16_t pmtPid,
        const PmtTable& table
    )
    {
        std::cout
            << "\n========== PMT ==========\n";

        std::cout
            << "Program: "
            << table.programNumber << '\n';

        std::cout
            << "Version: "
            << static_cast<int>(
                table.versionNumber
            ) << '\n';

        std::cout
            << "Current table: "
            << (table.currentNextIndicator
                    ? "YES"
                    : "NO")
            << '\n';

        std::cout << "PMT PID: ";

        printPid(pmtPid);

        std::cout << '\n';

        std::cout << "PCR PID: ";

        printPid(table.pcrPid);

        std::cout << '\n';

        for (const auto& stream :
             table.streams)
        {
            std::cout
                << PmtParser::categoryName(
                    stream.category
                )
                << " | "
                << PmtParser::streamTypeName(
                    stream.streamType
                )
                << " | PID ";

            printPid(stream.elementaryPid);

            std::cout << '\n';
        }

        std::cout << "CRC: VALID\n";

        std::cout
            << "=========================\n";
    }

    void printStatistics(
        const ReceiverStatistics& stats
    )
    {
        std::cout
            << "\n===== Receiver statistics =====\n";

        std::cout
            << "Received datagrams: "
            << stats.receivedDatagrams << '\n'

            << "Valid RTP packets: "
            << stats.validRtpPackets << '\n'

            << "Invalid RTP packets: "
            << stats.invalidRtpPackets << '\n'

            << "Unexpected SSRC packets: "
            << stats.unexpectedSsrcPackets << '\n'

            << "RTP discontinuities: "
            << stats.rtpDiscontinuities << '\n'

            << "Valid TS packets: "
            << stats.validTsPackets << '\n'

            << "Invalid TS packets: "
            << stats.invalidTsPackets << '\n'

            << "Transport errors: "
            << stats.transportErrors << '\n'

            << "Valid PAT sections: "
            << stats.validPatSections << '\n'

            << "Invalid PAT sections: "
            << stats.invalidPatSections << '\n'

            << "Valid PMT sections: "
            << stats.validPmtSections << '\n'

            << "Invalid PMT sections: "
            << stats.invalidPmtSections << '\n'

            << "Unexpected PMT sections: "
            << stats.unexpectedPmtSections << '\n'

            << "Forwarded bytes: "
            << stats.forwardedBytes << '\n'

            << "HLS write errors: "
            << stats.hlsWriteErrors << '\n'

            << "Receive timeouts: "
            << stats.receiveTimeouts << '\n';

        std::cout
            << "===============================\n";
    }
}

int main()
{
    std::signal(SIGINT, handleSignal);

    std::signal(SIGTERM, handleSignal);

    std::signal(SIGPIPE, SIG_IGN);

    std::cout
        << "IPTV M10 Live HLS Receiver\n\n";

    UdpSocket socket;

    if (!socket.open(RTP_PORT))
    {
        std::cerr
            << "Failed to open UDP socket\n";

        return 1;
    }

    if (!socket.setReceiveTimeout(
            RECEIVE_TIMEOUT_MS
        ))
    {
        std::cerr
            << "Failed to configure UDP timeout\n";

        return 1;
    }

    if (!socket.setReceiveBufferSize(
            RECEIVE_BUFFER_SIZE
        ))
    {
        std::cerr
            << "Warning: Cannot configure "
            << "UDP receive buffer\n";
    }

    HlsConfig hlsConfig{};

    hlsConfig.outputDirectory =
        "runtime/hls";

    hlsConfig.segmentDurationSeconds = 2;

    hlsConfig.playlistSize = 60;

    hlsConfig.deleteThreshold = 2;
    hlsConfig.playlistFilename = "producer.m3u8";
    hlsConfig.deleteSegments = false;

    // Remove files belonging to a previous
    // prototype playback session.

    std::error_code filesystemError;

    std::filesystem::remove_all(
        hlsConfig.outputDirectory,
        filesystemError
    );

    if (filesystemError)
    {
        std::cerr
            << "Failed to clean HLS directory: "
            << filesystemError.message()
            << '\n';

        return 1;
    }

    BufferConfig bufferConfig{};
    bufferConfig.storageDirectory = hlsConfig.outputDirectory;
    bufferConfig.deletionMode = SegmentDeletionMode::Deferred;
    // Short integration window; change to 1800000 for I9.10.8.
    bufferConfig.maxDurationMs = 30ULL * 60ULL * 1000ULL;
    HlsBufferManager manager(bufferConfig);
    if (!manager.initialize()) return 1;

    auto updateBuffer = [&]() -> bool
    {
        if (!manager.collectExpiredSegments()) return false;
        const auto result = manager.update();
        if (result.status == HlsUpdateStatus::PlaylistUnavailable)
            return true;
        if (result.status != HlsUpdateStatus::Updated &&
            result.status != HlsUpdateStatus::NoChanges)
        {
            std::cerr << "HLS buffer update failed: "
                      << static_cast<int>(result.status) << '\n';
            return false;
        }
        if (result.addedSegments || result.endList)
        {
            if (!HlsPlaylistWriter::write(
                    hlsConfig.outputDirectory / "index.m3u8",
                    manager.buffer().segments(), 2, true,
                    result.endList, manager.buffer().discontinuitySequence()))
                return false;
            manager.markPlaylistPublished();
        }
        return true;
    };
    auto lastBufferUpdate = std::chrono::steady_clock::now();

    HlsSegmenter segmenter;

    if (!segmenter.start(hlsConfig))
    {
        std::cerr
            << "Failed to start HLS segmenter\n";

        return 1;
    }

    PsiSectionAssembler patAssembler(
        TsParser::PAT_PID
    );

    std::map<
        std::uint16_t,
        PsiSectionAssembler
    > pmtAssemblers;

    std::map<
        std::uint16_t,
        std::uint16_t
    > programs;

    std::map<
        std::uint16_t,
        PmtTable
    > programTables;

    std::set<
        std::tuple<
            std::uint16_t,
            std::uint8_t,
            std::uint8_t,
            bool
        >
    > printedPatSections;

    std::set<
        std::tuple<
            std::uint16_t,
            std::uint16_t,
            std::uint8_t,
            bool
        >
    > printedPmtSections;

    auto resetAssemblers = [&]()
    {
        patAssembler.reset();

        for (auto& entry : pmtAssemblers)
        {
            entry.second.reset();
        }
    };

    ReceiverStatistics stats{};

    std::array<std::uint8_t, 65536>
        receiveBuffer{};

    std::array<std::uint8_t, 65536>
        forwardBuffer{};

    bool hasPreviousSequence = false;

    std::uint16_t previousSequence = 0;

    bool hasActiveSsrc = false;

    std::uint32_t activeSsrc = 0;

    bool signalAvailable = false;

    bool signalLossReported = false;

    bool fatalError = false;

    auto lastValidSignal =
        std::chrono::steady_clock::now();

    auto lastStatistics =
        std::chrono::steady_clock::now();

    std::cout
        << "Listening on UDP port "
        << RTP_PORT << '\n';

    std::cout
        << "Waiting for RTP signal...\n";

    std::cout
        << "Press Ctrl+C to stop\n";

    while (!stopRequested)
    {
        const auto now =
            std::chrono::steady_clock::now();

        if (!segmenter.isRunning())
        {
            std::cerr
                << "FFmpeg process stopped "
                << "unexpectedly\n";

            fatalError = true;

            break;
        }

        if (now - lastBufferUpdate >= std::chrono::milliseconds(250))
        {
            lastBufferUpdate = now;
            if (!updateBuffer())
            {
                fatalError = true;
                break;
            }
        }

        if (now - lastStatistics >=
            std::chrono::seconds(
                STATISTICS_INTERVAL_SECONDS
            ))
        {
            printStatistics(stats);
            const auto bufferStats = manager.buffer().statistics();
            std::cout << "Buffer duration ms: " << bufferStats.durationMs
                      << " | Active segments: " << bufferStats.segmentCount
                      << " | Pending deletion: " << manager.buffer().pendingDeletionCount()
                      << " | Total registered bytes: " << manager.buffer().totalStorageBytes()
                      << '\n';

            lastStatistics = now;
        }

        if (!signalLossReported &&
            now - lastValidSignal >=
                std::chrono::seconds(
                    SIGNAL_TIMEOUT_SECONDS
                ))
        {
            std::cout
                << "\n[WARNING] RTP signal "
                << "not available\n";

            signalAvailable = false;

            signalLossReported = true;

            resetAssemblers();
        }

        const ssize_t received =
            socket.receive(
                receiveBuffer.data(),
                receiveBuffer.size()
            );

        if (received < 0)
        {
            const int receiveError = errno;

            if (stopRequested)
            {
                break;
            }

            if (receiveError == EINTR)
            {
                continue;
            }

            if (receiveError == EAGAIN ||
                receiveError == EWOULDBLOCK)
            {
                ++stats.receiveTimeouts;

                continue;
            }

            std::cerr
                << "UDP receive error\n";

            fatalError = true;

            break;
        }

        ++stats.receivedDatagrams;

        RtpPacket rtpPacket{};

        if (!RtpParser::parse(
                receiveBuffer.data(),
                static_cast<std::size_t>(
                    received
                ),
                rtpPacket
            ) ||
            rtpPacket.payloadType !=
                RtpParser::MPEG_TS_PAYLOAD_TYPE ||
            rtpPacket.payloadSize == 0 ||
            rtpPacket.payloadSize %
                TsParser::PACKET_SIZE != 0)
        {
            ++stats.invalidRtpPackets;

            resetAssemblers();

            continue;
        }

        if (!hasActiveSsrc)
        {
            activeSsrc = rtpPacket.ssrc;

            hasActiveSsrc = true;

            std::cout
                << "\nActive RTP SSRC: "
                << activeSsrc << '\n';
        }

        if (rtpPacket.ssrc != activeSsrc)
        {
            ++stats.unexpectedSsrcPackets;

            continue;
        }

        ++stats.validRtpPackets;

        if (!signalAvailable)
        {
            std::cout
                << "\n[INFO] RTP signal available\n";

            signalAvailable = true;

            signalLossReported = false;
        }

        lastValidSignal =
            std::chrono::steady_clock::now();

        if (hasPreviousSequence)
        {
            const std::uint16_t expected =
                static_cast<std::uint16_t>(
                    previousSequence + 1
                );

            if (rtpPacket.sequenceNumber !=
                expected)
            {
                ++stats.rtpDiscontinuities;

                resetAssemblers();
            }
        }

        previousSequence =
            rtpPacket.sequenceNumber;

        hasPreviousSequence = true;

        std::size_t forwardSize = 0;

        for (std::size_t offset = 0;
             offset < rtpPacket.payloadSize;
             offset += TsParser::PACKET_SIZE)
        {
            const std::uint8_t* tsData =
                rtpPacket.payload + offset;

            TsPacket tsPacket{};

            if (!TsParser::parse(
                    tsData,
                    TsParser::PACKET_SIZE,
                    tsPacket
                ))
            {
                ++stats.invalidTsPackets;

                resetAssemblers();

                continue;
            }

            ++stats.validTsPackets;

            if (tsPacket.transportError)
            {
                ++stats.transportErrors;

                if (tsPacket.pid ==
                    TsParser::PAT_PID)
                {
                    patAssembler.reset();
                }

                auto assembler =
                    pmtAssemblers.find(
                        tsPacket.pid
                    );

                if (assembler !=
                    pmtAssemblers.end())
                {
                    assembler->second.reset();
                }

                continue;
            }

            // Data flow: preserve the complete
            // valid 188-byte MPEG-TS packet.

            for (std::size_t i = 0;
                 i < TsParser::PACKET_SIZE;
                 ++i)
            {
                forwardBuffer[forwardSize++] =
                    tsData[i];
            }

            // Metadata flow: PAT processing.

            if (tsPacket.pid ==
                TsParser::PAT_PID)
            {
                const auto sections =
                    patAssembler.push(tsPacket);

                for (const auto& section :
                     sections)
                {
                    PatTable table{};

                    if (!PatParser::parse(
                            section.data(),
                            section.size(),
                            table
                        ))
                    {
                        ++stats.invalidPatSections;

                        continue;
                    }

                    ++stats.validPatSections;

                    const auto key =
                        std::make_tuple(
                            table.transportStreamId,
                            table.versionNumber,
                            table.sectionNumber,
                            table.currentNextIndicator
                        );

                    if (printedPatSections
                            .insert(key).second)
                    {
                        printPat(table);
                    }

                    if (!table.currentNextIndicator)
                    {
                        continue;
                    }

                    for (const auto& program :
                         table.programs)
                    {
                        const auto previous =
                            programs.find(
                                program.programNumber
                            );

                        if (previous !=
                                programs.end() &&
                            previous->second !=
                                program.pmtPid)
                        {
                            programTables.erase(
                                program.programNumber
                            );
                        }

                        programs[
                            program.programNumber
                        ] = program.pmtPid;

                        pmtAssemblers.try_emplace(
                            program.pmtPid,
                            program.pmtPid
                        );
                    }
                }

                continue;
            }

            // Metadata flow: PMT processing.

            auto assembler =
                pmtAssemblers.find(
                    tsPacket.pid
                );

            if (assembler ==
                pmtAssemblers.end())
            {
                continue;
            }

            const auto sections =
                assembler->second.push(tsPacket);

            for (const auto& section :
                 sections)
            {
                PmtTable table{};

                if (!PmtParser::parse(
                        section.data(),
                        section.size(),
                        table
                    ))
                {
                    ++stats.invalidPmtSections;

                    continue;
                }

                const auto program =
                    programs.find(
                        table.programNumber
                    );

                if (program == programs.end() ||
                    program->second != tsPacket.pid)
                {
                    ++stats.unexpectedPmtSections;

                    continue;
                }

                ++stats.validPmtSections;

                const auto key =
                    std::make_tuple(
                        table.programNumber,
                        tsPacket.pid,
                        table.versionNumber,
                        table.currentNextIndicator
                    );

                if (printedPmtSections
                        .insert(key).second)
                {
                    printPmt(
                        tsPacket.pid,
                        table
                    );
                }

                if (table.currentNextIndicator)
                {
                    programTables[
                        table.programNumber
                    ] = table;
                }
            }
        }

        // Forward MPEG-TS without the RTP header.

        if (forwardSize > 0)
        {
            if (!segmenter.write(
                    forwardBuffer.data(),
                    forwardSize
                ))
            {
                ++stats.hlsWriteErrors;

                std::cerr
                    << "Failed to forward "
                    << "MPEG-TS to HLS segmenter\n";

                fatalError = true;

                break;
            }

            stats.forwardedBytes += forwardSize;
        }
    }

    std::cout
        << "\nStopping IPTV receiver...\n";

    socket.close();

    segmenter.stop();
    if (!fatalError && !updateBuffer()) fatalError = true;

    printStatistics(stats);

    std::cout
        << "Detected programs: "
        << programs.size() << '\n';

    std::cout
        << "Resolved PMT tables: "
        << programTables.size() << '\n';

    if (fatalError)
    {
        std::cerr
            << "Receiver stopped due to error\n";

        return 1;
    }

    std::cout
        << "Receiver stopped successfully\n";

    return 0;
}
