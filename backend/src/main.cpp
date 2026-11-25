#include "UdpSocket.hpp"
#include "RtpParser.hpp"
#include "TsParser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>

namespace
{
    constexpr std::uint16_t RTP_PORT = 5004;

    constexpr std::size_t TEST_PACKET_COUNT = 200;

    struct PidStatistics
    {
        std::size_t packetCount = 0;

        std::size_t payloadCount = 0;

        std::size_t payloadBytes = 0;

        std::size_t payloadUnitStarts = 0;

        std::size_t adaptationFields = 0;

        std::size_t pcrCount = 0;

        std::size_t transportErrors = 0;

        std::size_t continuityWarnings = 0;

        bool hasPreviousCounter = false;

        std::uint8_t previousCounter = 0;
    };
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

    std::cout << "IPTV M10 MPEG-TS Receiver\n";

    std::cout << "Listening on UDP port "
              << RTP_PORT << std::endl;

    std::array<std::uint8_t, 65536> buffer{};

    std::map<std::uint16_t, PidStatistics> pidStats;

    std::size_t validRtpPackets = 0;
    std::size_t invalidRtpPackets = 0;

    std::size_t validTsPackets = 0;
    std::size_t invalidTsPackets = 0;

    std::size_t transportErrors = 0;

    std::size_t rtpDiscontinuities = 0;

    std::uint16_t previousSequence = 0;

    bool hasPreviousSequence = false;

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
                static_cast<std::size_t>(receivedBytes),
                rtpPacket))
        {
            ++invalidRtpPackets;

            continue;
        }

        if (rtpPacket.payloadType !=
            RtpParser::MPEG_TS_PAYLOAD_TYPE)
        {
            ++invalidRtpPackets;

            continue;
        }

        if (rtpPacket.payloadSize == 0 ||
            rtpPacket.payloadSize %
                TsParser::PACKET_SIZE != 0)
        {
            ++invalidRtpPackets;

            continue;
        }

        ++validRtpPackets;

        // RTP sequence monitoring

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
            }
        }

        previousSequence =
            rtpPacket.sequenceNumber;

        hasPreviousSequence = true;

        // Parse individual MPEG-TS packets

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

                continue;
            }

            ++validTsPackets;

            PidStatistics& stats =
                pidStats[tsPacket.pid];

            ++stats.packetCount;

            if (tsPacket.transportError)
            {
                ++transportErrors;

                ++stats.transportErrors;

                stats.hasPreviousCounter = false;

                continue;
            }

            if (tsPacket.hasAdaptationField)
            {
                ++stats.adaptationFields;
            }

            if (tsPacket.hasPcr)
            {
                ++stats.pcrCount;
            }

            if (tsPacket.payloadUnitStart)
            {
                ++stats.payloadUnitStarts;
            }

            if (tsPacket.hasPayload)
            {
                ++stats.payloadCount;

                stats.payloadBytes +=
                    tsPacket.payloadSize;
            }

            // Basic continuity monitoring

            if (tsPacket.pid != TsParser::NULL_PID)
            {
                if (tsPacket.discontinuityIndicator)
                {
                    stats.hasPreviousCounter = false;
                }

                if (stats.hasPreviousCounter)
                {
                    const std::uint8_t expectedCounter =
                        static_cast<std::uint8_t>(
                            (stats.previousCounter +
                             (tsPacket.hasPayload ? 1 : 0))
                            & 0x0F
                        );

                    if (tsPacket.continuityCounter !=
                        expectedCounter)
                    {
                        ++stats.continuityWarnings;
                    }
                }

                stats.previousCounter =
                    tsPacket.continuityCounter;

                stats.hasPreviousCounter = true;
            }
        }
    }

    std::cout
        << "\n========== MPEG-TS TEST SUMMARY ==========\n";

    std::cout << "Received RTP datagrams: "
              << TEST_PACKET_COUNT << '\n';

    std::cout << "Valid RTP packets: "
              << validRtpPackets << '\n';

    std::cout << "Invalid RTP packets: "
              << invalidRtpPackets << '\n';

    std::cout << "RTP sequence discontinuities: "
              << rtpDiscontinuities << '\n';

    std::cout << "Valid TS packets: "
              << validTsPackets << '\n';

    std::cout << "Invalid TS packets: "
              << invalidTsPackets << '\n';

    std::cout << "Transport errors: "
              << transportErrors << '\n';

    std::cout << "Detected PIDs: "
              << pidStats.size() << '\n';

    std::cout
        << "\n=============== PID STATISTICS ===============\n";

    for (const auto& [pid, stats] : pidStats)
    {
        std::cout
            << "\nPID: 0x"
            << std::hex
            << std::setw(4)
            << std::setfill('0')
            << pid
            << std::dec
            << std::setfill(' ')
            << " (" << pid << ")";

        if (pid == TsParser::PAT_PID)
        {
            std::cout << " [PAT]";
        }
        else if (pid == TsParser::NULL_PID)
        {
            std::cout << " [NULL]";
        }

        std::cout << '\n';

        std::cout << "  Packets: "
                  << stats.packetCount << '\n';

        std::cout << "  Payload packets: "
                  << stats.payloadCount << '\n';

        std::cout << "  Payload bytes: "
                  << stats.payloadBytes << '\n';

        std::cout << "  PUSI count: "
                  << stats.payloadUnitStarts << '\n';

        std::cout << "  Adaptation fields: "
                  << stats.adaptationFields << '\n';

        std::cout << "  PCR count: "
                  << stats.pcrCount << '\n';

        std::cout << "  Transport errors: "
                  << stats.transportErrors << '\n';

        std::cout << "  Continuity warnings: "
                  << stats.continuityWarnings << '\n';
    }

    std::cout
        << "\n==============================================\n";

    return (
        invalidRtpPackets == 0 &&
        invalidTsPackets == 0 &&
        transportErrors == 0
    ) ? 0 : 1;
}
