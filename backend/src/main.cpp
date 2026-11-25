#include "UdpSocket.hpp"
#include "RtpParser.hpp"
#include "TsParser.hpp"
#include "PsiSectionAssembler.hpp"
#include "PatParser.hpp"
#include "PmtParser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <tuple>

namespace
{
    constexpr std::uint16_t RTP_PORT = 5004;

    constexpr std::size_t TEST_PACKET_COUNT = 200;

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
        std::cout << "\n========== PAT TABLE ==========\n";

        std::cout << "Transport Stream ID: "
                  << table.transportStreamId << '\n';

        std::cout << "Version: "
                  << static_cast<int>(
                         table.versionNumber
                     ) << '\n';

        std::cout << "Current table: "
                  << (table.currentNextIndicator
                      ? "Yes" : "No") << '\n';

        std::cout << "Section: "
                  << static_cast<int>(
                         table.sectionNumber
                     )
                  << "/"
                  << static_cast<int>(
                         table.lastSectionNumber
                     ) << '\n';

        std::cout << "Programs: "
                  << table.programs.size() << '\n';

        if (table.hasNetworkPid)
        {
            std::cout << "Network PID: ";

            printPid(table.networkPid);

            std::cout << '\n';
        }

        for (const auto& program : table.programs)
        {
            std::cout
                << "Program number: "
                << program.programNumber
                << " -> PMT PID: ";

            printPid(program.pmtPid);

            std::cout << '\n';
        }

        std::cout << "CRC32: VALID\n";

        std::cout << "===============================\n";
    }

    void printPmt(
        const PmtTable& table,
        std::uint16_t pmtPid
    )
    {
        std::cout << "\n========== PMT TABLE ==========\n";

        std::cout << "Program number: "
                  << table.programNumber << '\n';

        std::cout << "PMT PID: ";

        printPid(pmtPid);

        std::cout << '\n';

        std::cout << "Version: "
                  << static_cast<int>(
                         table.versionNumber
                     ) << '\n';

        std::cout << "Current table: "
                  << (table.currentNextIndicator
                      ? "Yes" : "No") << '\n';

        std::cout << "PCR PID: ";

        printPid(table.pcrPid);

        std::cout << '\n';

        std::cout << "Program info length: "
                  << table.programInfoLength << '\n';

        std::cout << "Elementary streams: "
                  << table.streams.size() << '\n';

        for (const auto& stream : table.streams)
        {
            std::cout << "\nStream category: "
                      << PmtParser::categoryName(
                             stream.category
                         ) << '\n';

            std::cout << "  Stream type: 0x"
                      << std::hex
                      << std::setw(2)
                      << std::setfill('0')
                      << static_cast<int>(
                             stream.streamType
                         )
                      << std::dec
                      << std::setfill(' ')
                      << " - "
                      << PmtParser::streamTypeName(
                             stream.streamType
                         ) << '\n';

            std::cout << "  Elementary PID: ";

            printPid(stream.elementaryPid);

            std::cout << '\n';

            std::cout << "  ES info length: "
                      << stream.esInfoLength << '\n';
        }

        std::cout << "\nCRC32: VALID\n";

        std::cout << "===============================\n";
    }
}

int main()
{
    UdpSocket socket;

    if (!socket.open(RTP_PORT))
    {
        std::cerr << "Failed to open UDP port "
                  << RTP_PORT << '\n';

        return 1;
    }

    std::cout << "IPTV M10 PAT/PMT Receiver\n";

    std::cout << "Listening on UDP port "
              << RTP_PORT << std::endl;

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

    std::map<
        std::uint16_t,
        std::size_t
    > pidCounts;

    using PatKey = std::tuple<
        std::uint16_t,
        std::uint8_t,
        std::uint8_t,
        bool
    >;

    using PmtKey = std::tuple<
        std::uint16_t,
        std::uint16_t,
        std::uint8_t,
        bool
    >;

    std::set<PatKey> printedPatSections;

    std::set<PmtKey> printedPmtSections;

    std::array<std::uint8_t, 65536> buffer{};

    std::size_t validRtpPackets = 0;
    std::size_t invalidRtpPackets = 0;
    std::size_t validTsPackets = 0;
    std::size_t invalidTsPackets = 0;
    std::size_t transportErrors = 0;
    std::size_t rtpDiscontinuities = 0;

    std::size_t patPackets = 0;
    std::size_t validPatSections = 0;
    std::size_t invalidPatSections = 0;

    std::size_t pmtPackets = 0;
    std::size_t validPmtSections = 0;
    std::size_t invalidPmtSections = 0;
    std::size_t unexpectedPmtSections = 0;

    std::uint16_t previousSequence = 0;

    bool hasPreviousSequence = false;

    std::uint32_t activeSsrc = 0;

    bool hasActiveSsrc = false;

    std::size_t unexpectedSsrcPackets = 0;

    const auto resetAssemblers = [&]()
    {
        patAssembler.reset();

        for (auto& entry : pmtAssemblers)
        {
            entry.second.reset();
        }
    };

    for (std::size_t i = 0;
         i < TEST_PACKET_COUNT;
         ++i)
    {
        const ssize_t receivedBytes =
            socket.receive(
                buffer.data(),
                buffer.size()
            );

        if (receivedBytes < 0)
        {
            std::cerr << "UDP receive error\n";

            return 1;
        }

        RtpPacket rtpPacket{};

        if (!RtpParser::parse(
                buffer.data(),
                static_cast<std::size_t>(
                    receivedBytes
                ),
                rtpPacket))
        {
            ++invalidRtpPackets;

            resetAssemblers();

            continue;
        }

        if (rtpPacket.payloadType !=
            RtpParser::MPEG_TS_PAYLOAD_TYPE ||
            rtpPacket.payloadSize == 0 ||
            rtpPacket.payloadSize %
                TsParser::PACKET_SIZE != 0)
        {
            ++invalidRtpPackets;

            resetAssemblers();

            continue;
        }

        if (!hasActiveSsrc)
        {
            activeSsrc = rtpPacket.ssrc;

            hasActiveSsrc = true;
        }

        if (rtpPacket.ssrc != activeSsrc)
        {
            ++unexpectedSsrcPackets;

            continue;
        }

        ++validRtpPackets;

        if (hasPreviousSequence)
        {
            const std::uint16_t expectedSequence =
                static_cast<std::uint16_t>(
                    previousSequence + 1
                );

            if (rtpPacket.sequenceNumber !=
                expectedSequence)
            {
                ++rtpDiscontinuities;

                resetAssemblers();
            }
        }

        previousSequence =
            rtpPacket.sequenceNumber;

        hasPreviousSequence = true;

        for (std::size_t offset = 0;
             offset < rtpPacket.payloadSize;
             offset += TsParser::PACKET_SIZE)
        {
            TsPacket tsPacket{};

            if (!TsParser::parse(
                    rtpPacket.payload + offset,
                    TsParser::PACKET_SIZE,
                    tsPacket))
            {
                ++invalidTsPackets;

                resetAssemblers();

                continue;
            }

            ++validTsPackets;

            ++pidCounts[tsPacket.pid];

            if (tsPacket.transportError)
            {
                ++transportErrors;

                if (tsPacket.pid == TsParser::PAT_PID)
                {
                    patAssembler.reset();
                }

                auto assembler =
                    pmtAssemblers.find(tsPacket.pid);

                if (assembler != pmtAssemblers.end())
                {
                    assembler->second.reset();
                }

                continue;
            }

            // PAT processing.

            if (tsPacket.pid == TsParser::PAT_PID)
            {
                ++patPackets;

                const auto sections =
                    patAssembler.push(tsPacket);

                for (const auto& section : sections)
                {
                    PatTable table{};

                    if (!PatParser::parse(
                            section.data(),
                            section.size(),
                            table))
                    {
                        ++invalidPatSections;

                        continue;
                    }

                    ++validPatSections;

                    const PatKey key{
                        table.transportStreamId,
                        table.versionNumber,
                        table.sectionNumber,
                        table.currentNextIndicator
                    };

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

                        if (previous != programs.end() &&
                            previous->second != program.pmtPid)
                        {
                            programTables.erase(
                                program.programNumber
                            );
                        }

                        programs[program.programNumber] =
                            program.pmtPid;

                        // Create an assembler only when
                        // the PMT PID is discovered.

                        pmtAssemblers.try_emplace(
                            program.pmtPid,
                            program.pmtPid
                        );
                    }
                }

                continue;
            }

            // PMT processing.

            auto assembler =
                pmtAssemblers.find(tsPacket.pid);

            if (assembler == pmtAssemblers.end())
            {
                continue;
            }

            ++pmtPackets;

            const auto sections =
                assembler->second.push(tsPacket);

            for (const auto& section : sections)
            {
                PmtTable table{};

                if (!PmtParser::parse(
                        section.data(),
                        section.size(),
                        table))
                {
                    ++invalidPmtSections;

                    continue;
                }

                const auto program =
                    programs.find(table.programNumber);

                if (program == programs.end() ||
                    program->second != tsPacket.pid)
                {
                    ++unexpectedPmtSections;

                    continue;
                }

                ++validPmtSections;

                const PmtKey key{
                    table.programNumber,
                    tsPacket.pid,
                    table.versionNumber,
                    table.currentNextIndicator
                };

                if (printedPmtSections
                        .insert(key).second)
                {
                    printPmt(table, tsPacket.pid);
                }

                if (table.currentNextIndicator)
                {
                    programTables[table.programNumber] =
                        table;
                }
            }
        }
    }

    std::cout
        << "\n========== PAT/PMT TEST SUMMARY ==========\n";

    std::cout << "Received RTP datagrams: "
              << TEST_PACKET_COUNT << '\n';

    std::cout << "Valid RTP packets: "
              << validRtpPackets << '\n';

    std::cout << "Invalid RTP packets: "
              << invalidRtpPackets << '\n';

    std::cout << "Unexpected SSRC packets: "
              << unexpectedSsrcPackets << '\n';

    std::cout << "RTP sequence discontinuities: "
              << rtpDiscontinuities << '\n';

    std::cout << "Valid TS packets: "
              << validTsPackets << '\n';

    std::cout << "Invalid TS packets: "
              << invalidTsPackets << '\n';

    std::cout << "Transport errors: "
              << transportErrors << '\n';

    std::cout << "PAT packets: "
              << patPackets << '\n';

    std::cout << "Valid PAT sections: "
              << validPatSections << '\n';

    std::cout << "Invalid PAT sections: "
              << invalidPatSections << '\n';

    std::cout << "PMT packets: "
              << pmtPackets << '\n';

    std::cout << "Valid PMT sections: "
              << validPmtSections << '\n';

    std::cout << "Invalid PMT sections: "
              << invalidPmtSections << '\n';

    std::cout << "Unexpected PMT sections: "
              << unexpectedPmtSections << '\n';

    std::cout << "Detected programs: "
              << programs.size() << '\n';

    std::cout << "Resolved PMT tables: "
              << programTables.size() << '\n';

    bool allProgramsResolved =
        !programs.empty();

    bool allProgramsHaveAudioVideo =
        !programs.empty();

    for (const auto& [programNumber, pmtPid] :
         programs)
    {
        std::cout << "\nProgram "
                  << programNumber << '\n';

        std::cout << "  PMT PID: ";

        printPid(pmtPid);

        std::cout << '\n';

        const auto table =
            programTables.find(programNumber);

        if (table == programTables.end())
        {
            std::cout << "  PMT not resolved\n";

            allProgramsResolved = false;

            allProgramsHaveAudioVideo = false;

            continue;
        }

        std::cout << "  PCR PID: ";

        printPid(table->second.pcrPid);

        std::cout << '\n';

        bool hasVideo = false;

        bool hasAudio = false;

        for (const auto& stream :
             table->second.streams)
        {
            std::cout
                << "  "
                << PmtParser::categoryName(
                       stream.category
                   )
                << " PID: ";

            printPid(stream.elementaryPid);

            std::cout
                << " - "
                << PmtParser::streamTypeName(
                       stream.streamType
                   )
                << '\n';

            if (stream.category ==
                StreamCategory::Video)
            {
                hasVideo = true;
            }

            if (stream.category ==
                StreamCategory::Audio)
            {
                hasAudio = true;
            }
        }

        if (!hasVideo || !hasAudio)
        {
            allProgramsHaveAudioVideo = false;
        }
    }

    std::cout << "\nDetected PID statistics:\n";

    for (const auto& [pid, count] : pidCounts)
    {
        std::cout << "PID ";

        printPid(pid);

        std::cout << ": "
                  << count << " packets\n";
    }

    const bool success =
        invalidRtpPackets == 0 &&
        invalidTsPackets == 0 &&
        unexpectedSsrcPackets == 0 &&
        transportErrors == 0 &&
        invalidPatSections == 0 &&
        invalidPmtSections == 0 &&
        unexpectedPmtSections == 0 &&
        validPatSections > 0 &&
        validPmtSections > 0 &&
        allProgramsResolved &&
        allProgramsHaveAudioVideo;

    std::cout << "\nResult: ";

    if (!success)
    {
        std::cout << "FAIL\n";
    }
    else if (rtpDiscontinuities > 0)
    {
        std::cout << "PASS WITH WARNINGS\n";
    }
    else
    {
        std::cout << "PASS\n";
    }

    std::cout
        << "==========================================\n";

    return success ? 0 : 1;
}
