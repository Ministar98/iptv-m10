#include "UdpSocket.hpp"
#include "RtpParser.hpp"
#include "TsParser.hpp"
#include "PsiSectionAssembler.hpp"
#include "PatParser.hpp"

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
            std::cout << "Network PID: "
                      << table.networkPid << '\n';
        }

        for (const auto& program : table.programs)
        {
            std::cout
                << "Program number: "
                << program.programNumber
                << " -> PMT PID: 0x"
                << std::hex
                << std::setw(4)
                << std::setfill('0')
                << program.pmtPid
                << std::dec
                << std::setfill(' ')
                << " (" << program.pmtPid << ")\n";
        }

        std::cout << "CRC32: VALID\n";

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

    std::cout << "IPTV M10 PAT Receiver\n";

    std::cout << "Listening on UDP port "
              << RTP_PORT << std::endl;

    PsiSectionAssembler patAssembler(
        TsParser::PAT_PID
    );

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

    std::uint16_t previousSequence = 0;

    bool hasPreviousSequence = false;

    std::uint32_t activeSsrc = 0;

    bool hasActiveSsrc = false;

    std::size_t unexpectedSsrcPackets = 0;

    std::map<std::uint16_t, std::size_t> pidCounts;

    std::map<std::uint16_t, std::uint16_t> programs;

    using PatKey = std::tuple<
        std::uint16_t,
        std::uint8_t,
        std::uint8_t,
        bool
    >;

    std::set<PatKey> printedSections;

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

            continue;
        }

        if (rtpPacket.payloadType !=
            RtpParser::MPEG_TS_PAYLOAD_TYPE ||
            rtpPacket.payloadSize == 0 ||
            rtpPacket.payloadSize %
                TsParser::PACKET_SIZE != 0)
        {
            ++invalidRtpPackets;

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

                patAssembler.reset();
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

                patAssembler.reset();

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

                continue;
            }

            if (tsPacket.pid != TsParser::PAT_PID)
            {
                continue;
            }

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

                if (printedSections.insert(key).second)
                {
                    printPat(table);
                }

                if (table.currentNextIndicator)
                {
                    for (const auto& program :
                         table.programs)
                    {
                        programs[program.programNumber] =
                            program.pmtPid;
                    }
                }
            }
        }
    }

    std::cout
        << "\n========== PAT TEST SUMMARY ==========\n";

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

    std::cout << "Detected programs: "
              << programs.size() << '\n';

    std::cout << "\nDetected program mapping:\n";

    for (const auto& [programNumber, pmtPid] :
         programs)
    {
        std::cout
            << "Program "
            << programNumber
            << " -> PMT PID "
            << pmtPid
            << '\n';
    }

    std::cout << "\nDetected PID statistics:\n";

    for (const auto& [pid, count] : pidCounts)
    {
        std::cout
            << "PID 0x"
            << std::hex
            << std::setw(4)
            << std::setfill('0')
            << pid
            << std::dec
            << std::setfill(' ')
            << ": "
            << count
            << " packets\n";
    }

    std::cout
        << "======================================\n";

    return (
        invalidRtpPackets == 0 &&
        invalidTsPackets == 0 &&
        unexpectedSsrcPackets == 0 &&
        transportErrors == 0 &&
        invalidPatSections == 0 &&
        validPatSections > 0 &&
        !programs.empty()
    ) ? 0 : 1;
}
