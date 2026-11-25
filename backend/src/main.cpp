#include "UdpSocket.hpp"
#include "RtpParser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace
{
    constexpr std::uint16_t RTP_PORT = 5004;

    constexpr std::size_t TS_PACKET_SIZE = 188;

    constexpr std::uint8_t TS_SYNC_BYTE = 0x47;

    constexpr std::size_t TEST_PACKET_COUNT = 20;

    bool validateTsPayload(const RtpPacket& packet)
    {
        if (packet.payload == nullptr ||
            packet.payloadSize == 0)
        {
            return false;
        }

        if (packet.payloadSize % TS_PACKET_SIZE != 0)
        {
            return false;
        }

        for (std::size_t offset = 0;
             offset < packet.payloadSize;
             offset += TS_PACKET_SIZE)
        {
            if (packet.payload[offset] != TS_SYNC_BYTE)
            {
                return false;
            }
        }

        return true;
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

    std::cout << "IPTV M10 RTP Receiver\n";

    std::cout << "Listening on UDP port "
              << RTP_PORT << std::endl;

    std::array<std::uint8_t, 65536> buffer{};

    std::size_t validPackets = 0;
    std::size_t invalidPackets = 0;

    std::size_t totalTsPackets = 0;
    std::size_t totalPayloadBytes = 0;

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

        RtpPacket packet{};

        if (!RtpParser::parse(
                buffer.data(),
                static_cast<std::size_t>(receivedBytes),
                packet))
        {
            std::cerr << "Invalid RTP packet\n";

            ++invalidPackets;

            continue;
        }

        if (packet.payloadType !=
            RtpParser::MPEG_TS_PAYLOAD_TYPE)
        {
            std::cerr << "Unexpected RTP payload type: "
                      << static_cast<int>(packet.payloadType)
                      << '\n';

            ++invalidPackets;

            continue;
        }

        if (!validateTsPayload(packet))
        {
            std::cerr << "Invalid MPEG-TS payload\n";

            ++invalidPackets;

            continue;
        }

        if (hasPreviousSequence)
        {
            const std::uint16_t expectedSequence =
                static_cast<std::uint16_t>(
                    previousSequence + 1
                );

            if (packet.sequenceNumber != expectedSequence)
            {
                std::cerr
                    << "RTP sequence discontinuity: "
                    << "expected=" << expectedSequence
                    << ", received=" << packet.sequenceNumber
                    << '\n';
            }
        }

        previousSequence = packet.sequenceNumber;

        hasPreviousSequence = true;

        const std::size_t tsPacketCount =
            packet.payloadSize / TS_PACKET_SIZE;

        ++validPackets;

        totalTsPackets += tsPacketCount;

        totalPayloadBytes += packet.payloadSize;

        std::cout
            << "RTP:"
            << " version=" << static_cast<int>(packet.version)
            << " PT=" << static_cast<int>(packet.payloadType)
            << " seq=" << packet.sequenceNumber
            << " timestamp=" << packet.timestamp
            << " SSRC=" << packet.ssrc
            << " header=" << packet.headerSize
            << " payload=" << packet.payloadSize
            << " TS packets=" << tsPacketCount
            << '\n';
    }

    std::cout << "\n========== RTP TEST SUMMARY ==========\n";

    std::cout << "Received datagrams: "
              << TEST_PACKET_COUNT << '\n';

    std::cout << "Valid RTP packets: "
              << validPackets << '\n';

    std::cout << "Invalid packets: "
              << invalidPackets << '\n';

    std::cout << "Total MPEG-TS packets: "
              << totalTsPackets << '\n';

    std::cout << "Total payload bytes: "
              << totalPayloadBytes << '\n';

    std::cout << "======================================\n";

    return invalidPackets == 0 ? 0 : 1;
}
